#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/targeting.hpp"
#include "server/systems/skills/evaluation.hpp"
#include <algorithm>

namespace d2x::server::effects {
DomainResult<> System::curse(const ActorContext &actor,const SkillCastSpec &skill,Vec center,EntityId selected) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
       !area || area->generation!=actor.areaGeneration || !skill.curse || player->persistent.player.mana<skill.manaCost)
        return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.curse;
    const auto *attracted=program.ai==CurseAi::Attract?ports_.monsters.find(selected):nullptr;
    const auto aiEligible=[](const monsters::Actor &m) {return skillAiControlAllowed(m.identity.rank,m.rule.switchAi,m.rule.boss);};
    if(program.ai==CurseAi::Attract && (!attracted || !attracted->enemyTarget() || attracted->life<=0 || attracted->area!=actor.area || !aiEligible(*attracted))) return {DomainStatus::Unavailable,{}};
    auto next=units_;
    std::map<EntityId,monsters::Actor::CurseControl> controls;
    std::vector<EntityId> attractions;
    transactions::CharacterEdit debit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    debit.player.mana-=skill.manaCost;debit.charge=skill.charge;
    for(const auto &[id,monster]:ports_.monsters.read().actors) {
        if(program.singleTarget && id!=selected) continue;
        if(!monster.enemyTarget() || monster.life<=0 || !monster.rule.curseable || monster.area!=actor.area || !(program.targetFilter&2) ||
           !area->definition.activation.nearby(center,monster.position) || (monster.rule.enchantment && monster.rule.enchantment->has(38))) continue;
        if(!withinSkillArea(center,monster.position,float(program.radius),SkillAreaCoordinates::TruncatedCells)) continue;
        if(program.ai!=CurseAi::None && !aiEligible(monster)) continue;
        if(program.targetLevelBonus && skill.rank+program.targetLevelBonus+player->persistent.player.level<=monster.rule.level) continue;
        if(program.skipActive && unitStates(id,actor.tick).contains(program.state.id)) continue;
        if(program.ai==CurseAi::Attract && id!=selected) {attractions.push_back(id);continue;}
        const int resistance=std::clamp(monster.petStats.attributes.combat.curseResistance+unitModifiers(id,actor.tick).combat.curseResistance,0,100);
        if(resistance==100) continue;
        auto &recipient=next[id];recipient.area=actor.area;
        if(recipient.states.size()>=128 && !recipient.states.hasState(program.state.id,actor.tick)) return {DomainStatus::Capacity,{}};
        CombatEffectSpec curse;curse.state=program.state;curse.stacking=EffectStacking::CurseLevel;
        curse.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
        const int divisor=program.ai==CurseAi::None || program.ignoreDifficultyDivisor?1:player->rules.skills->aiCurseDivisor.at(size_t(player->persistent.difficulty));
        if(!program.permanent) curse.duration=uint64_t(std::max<int64_t>(1,int64_t(program.frames/divisor)*(100-resistance)/100));curse.modifiers=program.modifiers;
        curse.curseAi=program.ai;
        // SkillNec tests STAT_UNITBASE independently for each immune channel.
        auto &modifiers=curse.modifiers;
        if(monster.rule.resistances[0]>=100) modifiers.combat.physicalResist/=5;
        if(monster.rule.resistances[2]>=100) modifiers.fireResist/=5;
        if(monster.rule.resistances[3]>=100) modifiers.lightningResist/=5;
        if(monster.rule.resistances[4]>=100) modifiers.coldResist/=5;
        if(monster.rule.resistances[5]>=100) modifiers.poisonResist/=5;
        const auto applied=recipient.states.apply(std::move(curse),actor.tick);if(!applied.accepted) continue;
        for(const auto &effect:recipient.states.entries()) if(effect.activeAt(actor.tick) && effect.spec.state.id==program.state.id) controls[id]={effect.spec.curseAi,effect.spec.source.entity,{},effect.expiresAt.value_or(UINT64_MAX),program.fearOrigin};
        for(const auto &removed:applied.removed) if(!recipient.states.hasState(removed.effect.spec.state.id,actor.tick)) {
            recipient.nativeStats.erase(removed.effect.spec.state.id);
            debit.publicFacts.emplace_back(StateFact{id,1,actor.area,removed.effect.spec.state.id,false});
        }
        CharacterModifiers installed;
        for(const auto &effect:recipient.states.entries()) if(effect.activeAt(actor.tick) && effect.spec.state.id==program.state.id) mergeCharacterModifiers(installed,effect.spec.modifiers);
        const std::pair<std::string_view,int> values[]{{"damageresist",installed.combat.physicalResist},{"damagepercent",installed.combat.damagePercent},
            {"item_tohit_percent",installed.combat.attackRatingPercent},{"skill_armor_percent",installed.combat.defensePercent},
            {"fireresist",installed.fireResist},{"lightresist",installed.lightningResist},{"coldresist",installed.coldResist},
            {"poisonresist",installed.poisonResist},{"velocitypercent",installed.velocityPercent},{"attackrate",installed.combat.attackRate}};
        auto &stats=recipient.nativeStats[program.state.id];stats.clear();
        for(const auto &[name,value]:values) if(value) stats.emplace_back(player->rules.skills->nativeStats.at(std::string(name)),value);
        debit.publicFacts.emplace_back(StateFact{id,1,actor.area,program.state.id,true,stats});
    }
    auto plan=ports_.transactions.prepare(std::move(debit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) {
        units_.swap(next);
        for(const auto &[id,control]:controls) {
            ports_.monsters.curseControl(id,control);ports_.monsters.stop(id);
            if(skill.missileNextDelay>0) ports_.monsters.hitDelay(id,actor.tick+uint64_t(skill.missileNextDelay));
        }
        if(program.ai==CurseAi::Attract) for(const auto id:attractions) ports_.monsters.attract(id,selected,actor.tick+uint64_t(std::max(1,program.frames/player->rules.skills->aiCurseDivisor.at(size_t(player->persistent.difficulty)))));
        for(const auto &[id,recipient]:units_) if(recipient.area==actor.area) ports_.monsters.velocityModifier(id,recipient.states.modifiers(actor.tick).velocityPercent);
    }
    (void)selected;return result;
}
int64_t System::physicalContactDamage(EntityId id,int64_t physical,uint64_t tick) const {
    const auto *player=ports_.players.findActor(id);if(!player) return int64_t(unitDamage(id,physical,DamageType::Physical,tick).dealt*256.f);
    float raw=float(physical)/256.f;
    if(const auto recovery=state_.players.find(id);recovery!=state_.players.end()) {
        auto states=recovery->second.states;std::vector<RemovedCombatEffect> removed;raw=states.absorbPhysical(raw,tick,removed);
        for(const auto &effect:states.entries()) if(effect.activeAt(tick) && effect.spec.source.kind==CombatEffectSource::Skill && player->rules.skills->definitions.contains(effect.spec.source.definition)) {
            const auto skill=skills::evaluate(*player,effect.spec.source.definition,effect.spec.source.level);
            if(skill.effect==SkillBehavior::EnergyShield) {raw=absorbSkillShield(raw,player->persistent.player.mana,skill.shieldPercent,skill.shieldManaFactor).damage;break;}
        }
    }
    return int64_t(mitigatePlayerDamage(raw,DamageType::Physical,player->totals.character).dealt*256.f);
}
void System::necroContact(EntityId source,EntityId target,int64_t physical,uint64_t tick,bool missile,int64_t previousLife) {
    if(physical<=0) return;
    const auto *attacker=ports_.monsters.find(source),*victim=ports_.monsters.find(target);
    const auto *player=ports_.players.findActor(source);
    const auto area=attacker?attacker->area:player?player->area:RegionId{};
    NecroContact contact{source,target,{},area,physical};
    if(!missile) if(const auto entry=units_.find(source);entry!=units_.end())
        for(const auto &effect:entry->second.states.entries()) if(effect.activeAt(tick) && effect.spec.modifiers.combat.ironMaidenPercent>0) {
            contact.reflect=effect.spec.modifiers.combat.ironMaidenPercent;contact.caster=effect.spec.source.entity;
            if(player || (attacker && attacker->hireling)) contact.reflect/=4;
        }
    contact.tap=unitModifiers(target,tick).combat.lifeTapPercent;
    if(contact.tap) if(const auto entry=units_.find(target);entry!=units_.end()) for(const auto &effect:entry->second.states.entries()) if(effect.activeAt(tick) && effect.spec.modifiers.combat.lifeTapPercent>0) {
        if(const auto *caster=ports_.players.findActor(effect.spec.source.entity);caster && caster->rules.skills) {
            const auto definition=caster->rules.skills->definitions.find(effect.spec.source.definition);
            if(definition!=caster->rules.skills->definitions.end() && definition->second.spec.curse) contact.overlay=definition->second.spec.curse->healOverlay;
        }
    }
    if(!missile) {
        const auto *clay=attacker && attacker->necroPet && attacker->necroPet->slowPercent>0?attacker:victim && victim->necroPet && victim->necroPet->slowPercent>0?victim:nullptr;
        if(clay) {contact.slow=clay->necroPet->slowPercent;contact.slowState=clay->necroPet->slowState.id;}
        if(attacker && attacker->necroPet && attacker->necroPet->lifeLeechPercent>0 && attacker->owner && victim) {
            contact.blood=std::min(previousLife,physical*std::max(0,victim->rule.drain)/100)*attacker->necroPet->lifeLeechPercent/100;
            contact.owner=*attacker->owner;contact.share=attacker->necroPet->ownerSharePercent;contact.bloodOverlay=attacker->necroPet->healOverlay;
        }
    }
    if(contact.reflect || contact.tap || contact.slow || contact.blood) necroContacts_.push_back(contact);
}
StepStatus System::advanceNecroContacts(uint64_t tick) {
    bool blocked=false;
    for(auto it=necroContacts_.begin();it!=necroContacts_.end();) {
        auto &contact=*it;const auto *source=ports_.monsters.find(contact.source);const auto *player=ports_.players.findActor(contact.source);
        if(!ports_.events.hasCapacity(4,2)) {blocked=true;++it;continue;}
        if(contact.tap>0) {
            DomainResult<> healing{DomainStatus::Applied,std::monostate{}};
            if(player && player->entered && player->area==contact.area && player->persistent.player.hp>0) {
                const auto &area=ports_.areas.at(contact.area);
                healing=restore({player->player,player->actor,player->area,area.generation,0,tick},float(contact.physical*contact.tap/100)/256.f,0,0,{});
            } else if(source && source->life>0) healing=ports_.monsters.heal(source->id,contact.physical*contact.tap/100);
            if(healing.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(healing && contact.overlay>=0) ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},contact.area},{OverlayFact{contact.source,uint8_t(player?0:1),contact.area,contact.overlay}}});
            contact.tap=0;
        }
        if(contact.reflect>0 && source && source->life>0) {
            combat::SpellImpact reflected{contact.source,contact.caster,contact.area,DamageType::Physical,std::min<int64_t>(INT32_MAX,contact.physical*contact.reflect/100),{contact.source}};
            reflected.occurrence=(uint64_t(1)<<59)|(++necroOccurrence_);reflected.reaction=true;reflected.unblockable=true;reflected.hitClass=0x8d;
            const auto result=ports_.combat.enqueue(std::move(reflected));
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}contact.reflect=0;
        }
        if(contact.slow>0) {
            const auto *victim=ports_.monsters.find(contact.target);
            const auto id=source && source->necroPet && source->necroPet->slowPercent>0?contact.target:contact.source;
            const auto result=ports_.monsters.slow(id,contact.slowState,contact.slow,50,tick);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            (void)victim;contact.slow=0;
        }
        if(contact.blood>0) {
            const auto *owner=ports_.players.find(contact.owner);
            const auto ownerHeal=[&](int64_t requested) -> DomainResult<int64_t> {
                if(!owner || !owner->entered || owner->persistent.player.hp<=0 || owner->area!=contact.area) return {DomainStatus::Applied,int64_t(0)};
                const auto amount=std::min(requested,int64_t(owner->totals.character.maxLife*256)-int64_t(owner->persistent.player.hp*256));
                const auto &area=ports_.areas.at(contact.area);
                const auto result=restore({owner->player,owner->actor,contact.area,area.generation,0,tick},float(amount)/256.f,0,0,{});
                return {result.status,result?std::optional{amount}:std::nullopt};
            };
            if(!contact.bloodPhase) {const auto result=ownerHeal(contact.blood*contact.share/100);if(!result) {blocked=true;++it;continue;}contact.blood-=*result.value;contact.bloodPhase=1;}
            if(contact.bloodPhase==1) {
                const auto amount=source && source->life>0?std::min(contact.blood,source->maximumLife-source->life):0;
                const auto result=amount?ports_.monsters.heal(contact.source,amount):DomainResult<>{DomainStatus::Applied,std::monostate{}};
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}contact.blood-=amount;contact.bloodPhase=2;
            }
            if(contact.blood>0) {const auto result=ownerHeal(contact.blood);if(!result) {blocked=true;++it;continue;}}
            if(contact.bloodOverlay>=0) {ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},contact.area},{OverlayFact{contact.source,1,contact.area,contact.bloodOverlay}}});if(owner) ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},contact.area},{OverlayFact{owner->actor,0,contact.area,contact.bloodOverlay}}});}
        }
        it=necroContacts_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
