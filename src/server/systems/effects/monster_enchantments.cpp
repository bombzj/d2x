#include "system.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::castMonsterCurse(EntityId source,const AuraDefinition &a,EntityId target,uint64_t tick,bool unique,const SkillFormulaSnapshot *sourceInput) {
    const auto *emitter=ports_.monsters.find(source);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto recipient=participants.find(target);
    if(!emitter || !emitter->rule.stats || !emitter->rule.formulas || !participants.canHarm(source,target)) return {DomainStatus::InvalidActor,{}};
    const auto sampled=unitStatValues(source,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
    auto context=sourceInput?sourceInput->input(*emitter->rule.stats):skills::formulaInput(*emitter,*sampled);
    SkillFormulaValues formulas(*emitter->rule.formulas,context);
    const auto length=formulas.calculation(FormulaDomain::Skill,a.skill,a.rank,"auralencalc");
    if(!length) return {DomainStatus::Unavailable,{}};
    CurseSpec program;program.state=a.state;program.rejectZeroPrimary=!unique;program.nativeValues.emplace();program.modifiers=a.modifiers;
    program.primaryStat=formulas.auraStatKey(a.skill,1);
    program.nativeWrites=formulas.auraStats(a.skill,a.rank,6,false,true);
    for(auto &write:program.nativeWrites) {
        if(!unique && recipient.monster && recipient.monster->nativeStats && recipient.monster->rule.stats) {
            const auto &rules=*recipient.monster->rule.stats;
            const auto &name=rules.definitions.at(write.key.id).name;
            if(name=="damageresist" || name=="magicresist" || name=="fireresist" || name=="lightresist" || name=="coldresist" || name=="poisonresist")
                write.value=curseResistanceAdjustment(write.value,baseStatValue(rules,*recipient.monster->nativeStats->sources,write.key),true,recipient.monster->hireling);
        }
        program.nativeValues->insert_or_assign(write.key,write.value);
    }
    program.modifiers=nativeCharacterModifiers(StatSnapshot{*program.nativeValues},*emitter->rule.stats,a.modifiers);
    CombatEffectSpec spec;spec.state=a.state;spec.source={CombatEffectSource::Skill,source,a.skill,a.rank};
    spec.stacking=EffectStacking::CurseLevel;spec.modifiers=program.modifiers;spec.nativeValues=program.nativeValues;
    const auto duration=curseStateDuration(*length,recipient.player?recipient.player->totals.character.combat.curseResistance:unitCurseResistance(target,tick),a.state.curse);
    if(!duration) return {DomainStatus::Unavailable,{}};
    if(*duration) spec.duration=uint32_t(*duration);
    if(recipient.player) {
        const auto &player=*recipient.player;auto next=state_.players;auto &states=next[target].states;
        PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(player.area));
        const auto application=applyCurseState(states,program,spec,program.nativeWrites,*emitter->rule.stats,tick,[&]{resources.sample(states,true);});
        if(!application.accepted) return {DomainStatus::Unavailable,{}};
        transactions::CharacterEdit edit{{player.player,target,player.area,ports_.areas.at(player.area).generation,0,tick},player.inventoryRevision,player.characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,tick);
        for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,tick)) edit.publicFacts.emplace_back(StateFact{target,0,player.area,removed.effect.spec.state.id,false});
        const auto *installed=states.find(application.handle);if(!installed) throw std::runtime_error("Missing native monster curse recipient");
        edit.publicFacts.emplace_back(StateFact{target,0,player.area,a.state.id,true,nativeEffectStats(*player.rules.skills,installed->spec)});
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next);return result;
    }
    const auto *monster=recipient.monster;
    if(!monster || !monster->alive() || (!unique && !monster->rule.curseable) || !monster->rule.stateTarget.acceptsCurseList() || !monster->nativeStats || !monster->rule.stats ||
       (!unique && monster->rule.enchantment && monster->rule.enchantment->has(38))) return {DomainStatus::Unavailable,{}};
    auto next=units_;auto &unit=next[target];unit.area=monster->area;
    MonsterResourceTrace resources(*monster,tick,&unit.states);
    const auto applied=applyCurseState(unit.states,program,spec,program.nativeWrites,*emitter->rule.stats,tick,[&]{resources.sample(unit.states,true);});
    if(!applied.accepted) return {DomainStatus::Unavailable,{}};
    auto publication=std::make_shared<AuraUnitPublication>();
    for(const auto &removed:applied.removed) if(!unit.states.hasState(removed.effect.spec.state.id,tick)) {
        unit.nativeStats.erase(removed.effect.spec.state.id);publication->facts.emplace_back(StateFact{target,1,monster->area,removed.effect.spec.state.id,false});
    }
    const auto *installed=unit.states.find(applied.handle);if(!installed) throw std::runtime_error("Missing native monster curse state");
    const auto values=combatEffectNativeValues(installed->spec);std::vector<StatValue> stats;
    if(values) for(const auto &[key,value]:*values) stats.emplace_back(key,value);
    unit.nativeStats[a.state.id]=stats;publication->facts.emplace_back(StateFact{target,1,monster->area,a.state.id,true,std::move(stats)});
    prepareUnitEffects(*publication,target,unit,resources.take(),tick);
    if(!validateAuraUnits(*publication)) return {DomainStatus::Conflict,{}};
    const auto accepted=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},monster->area},std::move(publication->facts)});
    if(!accepted) return {accepted.status,{}};
    publishAuraUnits(*publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::monsterAura(EntityId source,const AuraDefinition &a,EntityId id,uint64_t tick,uint64_t duration,bool owner) {
    const auto *m=ports_.monsters.find(source);if(!m) return {DomainStatus::InvalidActor,{}};
    if(a.ownerWrites || a.targetWrites)
        return paladinAura({{},source,m->area,ports_.areas.at(m->area).generation,0,tick},a,id,duration,owner,false,source);
    const auto state=owner?a.ownerState:a.state;
    if(state.id<0) return {DomainStatus::Applied,std::monostate{}};
    CombatEffectSpec spec;spec.state=state;spec.modifiers=owner?auraOwnerModifiers(a):a.modifiers;spec.duration=duration;
    spec.source={CombatEffectSource::Monster,source,a.skill,a.rank};
    spec.stacking=EffectStacking::AuraLevel;
    for(const auto &[key,p]:ports_.players.all()) if(p.actor==id && p.entered && p.area==m->area && p.persistent.player.hp>0) {
        if(a.skill==123 && p.rules.skills && p.rules.skills->auras.contains(123)) {
            const auto state=p.rules.skills->auras.at(123).spec.ownerState.id;
            const auto effects=state_.players.find(p.actor);
            if(effects!=state_.players.end()) for(const auto &effect:effects->second.states.entries())
                if(effect.activeAt(tick) && effect.spec.state.id==state && effect.spec.source.level>a.rank) return {DomainStatus::Applied,std::monostate{}};
        }
        return apply({key,id,p.area,ports_.areas.at(p.area).generation,0,tick},std::move(spec));
    }
    const auto *target=ports_.monsters.find(id);
    if(!target || target->area!=m->area || target->life<=0) return {DomainStatus::InvalidActor,{}};
    if(state.curse && target->rule.enchantment && target->rule.enchantment->has(38)) return {DomainStatus::Applied,std::monostate{}};
    if(a.skill==123) {
        const auto previous=stateModifiers(id,state.id,tick),existing=unitModifiers(id,tick);
        if(target->rule.resistances[2]+existing.fireResist-previous.fireResist>=100) spec.modifiers.fireResist/=5;
        if(target->rule.resistances[3]+existing.lightningResist-previous.lightningResist>=100) spec.modifiers.lightningResist/=5;
        if(target->rule.resistances[4]+existing.coldResist-previous.coldResist>=100) spec.modifiers.coldResist/=5;
    }
    auto next=units_;auto &unit=next[id];unit.area=m->area;
    std::optional<MonsterResourceTrace> resources;
    if(target->nativeStats && target->nativeStats->sources && target->rule.stats) resources.emplace(*target,tick,&unit.states);
    const auto application=unit.states.apply(std::move(spec),tick,[&]{if(resources) resources->sample(unit.states,true);});
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    if(resources) {
        resources->sample(unit.states);
        if(!ports_.monsters.canCommitLife(resources->read())) return {DomainStatus::Conflict,{}};
    }
    std::vector<StatValue> stats;
    if(const auto found=m->rule.auraStats.find(a.skill);found!=m->rule.auraStats.end()) stats=owner?found->second.owner:found->second.targets;
    const auto previous=unit.nativeStats.find(state.id);
    const bool changed=previous==unit.nativeStats.end() || previous->second!=stats || !unitStates(id,tick).contains(state.id);
    unit.nativeStats[state.id]=stats;
    std::vector<DomainFact> facts;
    for(const auto &removed:application.removed) if(!unit.states.hasState(removed.effect.spec.state.id,tick))
        facts.emplace_back(StateFact{id,1,m->area,removed.effect.spec.state.id,false});
    if(changed) facts.emplace_back(StateFact{id,1,m->area,state.id,true,std::move(stats)});
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents());
    if(!facts.empty() && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    ports_.frameEvents.publish(std::move(timers));units_.swap(next);
    if(resources && !ports_.monsters.commitLife(resources->take())) throw std::logic_error("Native monster aura resource commit lost its reservation");
    ports_.monsters.velocityModifier(id,units_.at(id).states.modifiers(tick).velocityPercent);
    return {DomainStatus::Applied,std::monostate{}};
}
void System::triggerMonsterCurse(EntityId id,uint64_t tick) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->rule.enchantment || !m->rule.enchantment->curse) return;
    auto [it,fresh]=uniqueCycles_.try_emplace(id);auto &cycle=it->second;
    // UMod7's native callback casts on three of four rolls around the owner.
    auto random=m->combatRandom;const bool cast=(rollRandom(random)&3)!=0;
    if(!ports_.monsters.consumeRandom(id,m->combatRandom,random)) return;
    if(!cast) return;
    if(!m->rule.stats || !m->rule.formulas) return;
    const auto values=unitStatValues(id,tick);if(!values) return;
    auto aura=*m->rule.enchantment->curse;
    aura.rank=std::max(1,logicalStatValue(*values,*m->rule.stats,"level")/5+1);
    auto context=skills::formulaInput(*m,*values);SkillFormulaValues formulas(*m->rule.formulas,context);
    const auto range=formulas.calculation(FormulaDomain::Skill,aura.skill,aura.rank,"aurarangecalc");
    if(!range) return;
    const auto &area=ports_.areas.at(m->area);Vec origin=m->position+area.definition.origin;
    if(m->movementTarget) {
        const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(m->movementTarget);
        if(target.player) origin=target.position()+ports_.areas.at(target.player->area).definition.origin;
        else if(target.monster) origin=target.position()+ports_.areas.at(target.monster->area).definition.origin;
    } else origin=m->movementGoal+area.definition.origin;
    PendingMonsterCurse pending{aura,captureSkillFormulaInput(context),
        nativeAuraCandidateRecipients({id,m->area,std::clamp(*range,1,40),3,false,origin,NativeAuraTargetMode::TargetRange}),0,tick};
    monsterCurses_.emplace_back(id,std::move(pending));
}
StepStatus System::advanceMonsterCurses(uint64_t tick) {
    while(!monsterCurses_.empty()) {
        auto &[source,curse]=monsterCurses_.front();
        while(curse.next<curse.targets.size()) {
            const auto result=castMonsterCurse(source,curse.aura,curse.targets[curse.next],curse.tick,true,&curse.source);
            if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
            ++curse.next;
        }
        monsterCurses_.pop_front();
    }
    return StepStatus::Complete;
}
StepStatus System::advanceMonsterEnchantments(EntityId only,uint64_t tick) {
    bool blocked=false;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    for(auto current=ports_.monsters.read().actors.find(only);current!=ports_.monsters.read().actors.end() && current->first==only;++current) {
        const auto &[id,m]=*current;
        if(m.owner || (!m.rule.enchantment && !m.rule.nativeAura && !m.rule.hitBurst && !m.rule.deathBurst)) continue;
        const MonsterEnchantment empty;
        const auto &mods=m.rule.enchantment?*m.rule.enchantment:empty;const auto &area=ports_.areas.at(m.area);
        auto [entry,fresh]=uniqueCycles_.try_emplace(id);auto &cycle=entry->second;
        auto burst=[&](int definition,bool dead)->DomainResult<> {
            const auto found=m.rule.enchantmentMissiles.find(definition);if(found==m.rule.enchantmentMissiles.end()) return {DomainStatus::Unavailable,{}};
            MonsterAttackRule attack;attack.missile=found->second;
            missiles::MonsterSpawn spawn{id,m.area,area.generation,tick,m.position,m.position,attack,m.rule.hitStates,m.rule.level,0,0};spawn.postMortem=dead;
            if(definition==195) for(const auto &ray:monsterLightningRays()) spawn.aims.push_back(m.position+ray.offset);
            else for(const auto heading:missileRingBurst(2)) spawn.aims.push_back(m.position+heading);
            const auto result=ports_.missiles.spawnMonster(spawn);
            return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
        };
        auto nativeBurst=[&](const MonsterMissileRule &rule,bool dead)->DomainResult<> {
            MonsterAttackRule attack;attack.missile=rule;
            missiles::MonsterSpawn spawn{id,m.area,area.generation,tick,m.position,m.position,attack,m.rule.hitStates,m.rule.level,0,0};spawn.postMortem=dead;
            for(const Vec offset:std::array<Vec,4>{Vec{0,-1},Vec{1,0},Vec{0,1},Vec{-1,0}}) for(int branch=0;branch<2;++branch) spawn.aims.push_back(m.position+offset);
            const auto result=ports_.missiles.spawnMonster(spawn);
            return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
        };
        if(m.rule.hitBurst && m.hitOccurrence!=cycle.nativeHit) {cycle.nativeHit=m.hitOccurrence;if(m.reactionMode==3 && m.reactionUntil>tick && m.knockedUntil<=tick) cycle.nativeHitAt=tick+2;}
        if(cycle.nativeHitAt && tick>=cycle.nativeHitAt) {
            const auto result=nativeBurst(*m.rule.hitBurst,m.life<=0);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            cycle.nativeHitAt=0;
        }
        if(m.life<=0 && (m.rule.hitBurst || m.rule.deathBurst) && m.deathOccurrence!=cycle.nativeDeath) {cycle.nativeDeath=m.deathOccurrence;cycle.nativeDeathAt=m.deathTick+(m.rule.hitBurst?2:0);}
        if(cycle.nativeDeathAt && tick>=cycle.nativeDeathAt) {
            const auto result=nativeBurst(m.rule.deathBurst?*m.rule.deathBurst:*m.rule.hitBurst,true);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            cycle.nativeDeathAt=0;
        }
        const bool unique=nativeUniqueMonster(m.identity.rank);
        if(unique && mods.has(17) && m.damageOccurrence!=cycle.damage) {
            cycle.damage=m.damageOccurrence;
            const bool getHit=m.reactionUntil>tick && m.reactionMode==3;
            cycle.lightning=tick+(getHit?2:0);
        }
        if(cycle.lightning && tick>=cycle.lightning) {
            if(!ports_.events.hasCapacity(2)) {blocked=true;continue;}
            bool emitted=false;
            if(tick>=std::max(uint64_t(10),cycle.nextLightning)) {
                const auto result=burst(195,m.life<=0);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
                emitted=bool(result);if(emitted) cycle.nextLightning=tick+10;
            ports_.monsters.lightningEmission(id,emitted,tick);
            cycle.lightning=0;
            }
        }
        if(m.life<=0 && m.deathOccurrence!=cycle.deathOccurrence) {cycle.deathOccurrence=m.deathOccurrence;cycle.death=m.deathTick+4;cycle.deathPhase=0;}
        if(cycle.death && tick>=cycle.death) {
            if(cycle.deathPhase==0) {
                if(mods.has(9)) {
                    auto random=m.combatRandom;
                    const int low=int(mods.corpseExplosionMinimum*4),high=int(mods.corpseExplosionMaximum*4);
                    const int64_t damage=int64_t(low+(high>low?limitedRandom(random,unsigned(high-low)):0))*64;
                    std::vector<EntityId> targets;
                    auto eligible=[&](Vec p){const Vec delta{std::floor(p.x)-std::floor(m.position.x),std::floor(p.y)-std::floor(m.position.y)};return delta.length()<=m.rule.difficulty+4 && area.definition.activation.nearby(p,m.position) && area.definition.collision.missileSegment(m.position,p,{0x0805,1});};
                    for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m.area && p.persistent.player.hp>0 && eligible(p.position)) targets.push_back(p.actor);}
                    for(const auto &[key,p]:ports_.monsters.read().actors) if(participants.canHarm(id,key) && p.life>0 && p.area==m.area && eligible(p.position)) targets.push_back(key);
                    combat::SpellImpact impact{id,id,m.area,DamageType::Physical,0,std::move(targets)};impact.occurrence=(uint64_t(1)<<62)|m.deathOccurrence;impact.reaction=true;impact.unblockable=true;
                    impact.monsterHit=MonsterHit{};impact.monsterHit->channels[0]=damage;impact.monsterHit->channels[2]=damage;impact.monsterStates=m.rule.hitStates;
                    auto plan=ports_.combat.prepareSpells({impact});if(!plan || !ports_.events.hasCapacity(1)) {blocked=true;continue;}
                    // Original fire-enchanted death uses the original visual missile.
                    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m.area},{MissileFact{id,1,m.area,117,1,0,m.position,m.position}}})) {blocked=true;continue;}
                    const auto consumed=ports_.monsters.consumeRandom(id,m.combatRandom,random);
                    if(!consumed) throw std::logic_error("Monster death burst lost its reserved seed");
                    ports_.combat.commitSpells(std::move(*plan.value));
                }
                cycle.deathPhase=1;
            }
            if(cycle.deathPhase==1 && unique && mods.has(18)) {const auto result=burst(194,true);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}}
            cycle.death=0;cycle.deathPhase=2;
        }

    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
