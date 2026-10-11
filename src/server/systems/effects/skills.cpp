#include "system.hpp"
#include "gameplay/combat/life.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
namespace d2x::server::effects {
namespace {
TransientAttributes projection(const CombatEffectSet &states, uint64_t tick) {
    TransientAttributes result;result.modifiers=states.modifiers(tick);
    for(const auto &e:states.entries()) if(e.activeAt(tick) && e.spec.state.id>=0) result.states.insert(e.spec.state.id);
    return result;
}
std::optional<AnimationTiming> reactionTiming(const PlayerState &player,std::string_view mode) {
    if(!player.rules.skills) return {};
    const auto found=player.rules.skills->recoveryAnimations.find(std::string(mode=="kb"?"gh":mode)+player.totals.equipment.animationClass);
    if(found==player.rules.skills->recoveryAnimations.end()) return {};
    auto timing=found->second;
    // UNITS_GetBaseAnimSpeed: player KB uses the unmodified walk clock.
    if(mode=="kb") {timing.speed=213;return timing;}
    const bool block=mode=="bl";
    const bool holyShield=block && player.transient.states.contains(player.rules.skills->holyShield.id);
    timing.speed=effectiveRecoverySpeed(timing.speed,block?player.totals.character.combat.fasterBlock:player.totals.character.combat.fasterHitRecovery,holyShield);
    return timing;
}
}
bool System::recovering(EntityId id,uint64_t tick) const {
    const auto found=state_.players.find(id);
    return found!=state_.players.end() && found->second.actionUntil>tick;
}
DomainResult<> System::block(const ActorContext &actor) {
    const auto *player=ports_.players.find(actor.player);
    const auto *area=ports_.areas.find(actor.area);
    if(!player || !area || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
        area->generation!=actor.areaGeneration || player->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    const auto timing=reactionTiming(*player,"bl");
    if(!timing || ports_.skills.uninterruptible(actor.actor)) return {DomainStatus::Applied,std::monostate{}};
    auto next=state_;
    auto &recovery=next.players[actor.actor];
    // SUNITDMG: repeated successful blocks suppress BL, never the block itself.
    const uint64_t delay=uint64_t(std::max(0,player->totals.character.combat.fasterBlock/8+15));
    if(actor.tick<=recovery.lastBlockTick || actor.tick-recovery.lastBlockTick<=delay) return {DomainStatus::Applied,std::monostate{}};
    HitFact fact{actor.actor,0,actor.area,playerLifePercentage(int64_t(player->persistent.player.hp*256),int64_t(player->totals.character.maxLife)*256),false,player->position};
    fact.playerMode=18;
    recovery.lastBlockTick=actor.tick;
    recovery.actionUntil=actor.tick+std::min<uint64_t>(timing->durationTicks(),UINT64_MAX-actor.tick);
    const auto result=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{fact}});
    if(!result) return {result.status,{}};
    state_.players.swap(next.players);
    ports_.skills.interrupt(actor);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::skill(const ActorContext &actor, const SkillCastSpec &skill, std::optional<PlayerId> recipient) {
    const auto *caster=ports_.players.find(actor.player);const auto *target=ports_.players.find(recipient.value_or(actor.player));
    if(!caster || !target || !caster->entered || !target->entered || caster->actor!=actor.actor || caster->area!=actor.area ||
        target->area!=actor.area || caster->persistent.player.hp<=0 || target->persistent.player.hp<=0 || !skill.appliedEffect ||
        caster->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &recovery=next.players[target->actor];
    if(recovery.states.size()>=128) return {DomainStatus::Capacity,{}};
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,caster->actor,skill.sourceId,skill.rank};
    if(!recovery.states.apply(std::move(spec),actor.tick).accepted) return {DomainStatus::Conflict,{}};
    recovery.previous=target->position;
    const auto &area=ports_.areas.at(target->area);
    const ActorContext targetActor{target->player,target->actor,target->area,area.generation,0,actor.tick};
    transactions::CharacterEdit edit{targetActor,target->inventoryRevision,target->characterRevision,target->persistent.player};
    edit.transient=projection(recovery.states,actor.tick);
    if(skill.effect!=SkillBehavior::BoneArmor) {
        auto stats=nativeEffectStats(*caster->rules.skills,skill.appliedEffect->modifiers);
        stats.emplace_back(caster->rules.skills->nativeStats.at("modifierlist_skill"),skill.sourceId);
        stats.emplace_back(caster->rules.skills->nativeStats.at("modifierlist_level"),skill.rank);
        edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,skill.appliedEffect->state.id,true,std::move(stats)});
    }
    if(skill.effect==SkillBehavior::BoneArmor) edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,skill.appliedEffect->state.id,true,
        {{caster->rules.skills->nativeStats.at("bonearmor"),skill.appliedEffect->physicalShield},
         {caster->rules.skills->nativeStats.at("bonearmormax"),skill.appliedEffect->physicalShieldMaximum}}});
    if(skill.effect==SkillBehavior::BoneArmor && skill.castOverlayId>=0) edit.publicFacts.emplace_back(OverlayFact{target->actor,0,target->area,skill.castOverlayId});
    std::vector<transactions::Plan> plans;
    if(target==caster) {edit.player.mana-=skill.manaCost;edit.charge=skill.charge;}
    else {
        transactions::CharacterEdit debit{actor,caster->inventoryRevision,caster->characterRevision,caster->persistent.player};
        debit.player.mana-=skill.manaCost;debit.charge=skill.charge;
        auto prepared=ports_.transactions.prepare(std::move(debit));if(!prepared) return {prepared.status,{}};
        plans.push_back(std::move(*prepared.value));
    }
    auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
    plans.push_back(std::move(*prepared.value));
    const auto result=plans.size()==1?ports_.transactions.commit(std::move(plans.front())):ports_.transactions.commitCharacters(std::move(plans));if(result) state_.players.swap(next.players);
    return result;
}
DomainResult<float> System::receive(const ActorContext &actor, int64_t raw, DamageType type) {
    MonsterHit hit; hit.channels[size_t(type)] = raw;
    return receiveMonster(actor, {}, hit, {});
}
DomainResult<float> System::receiveMonster(const ActorContext &actor, EntityId source, const MonsterHit &hit, const MonsterHitStates &definitions,std::optional<ItemHandle> wear,std::optional<std::reference_wrapper<uint64_t>> contactRandom) {
    const auto *p = ports_.players.find(actor.player);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area || p->persistent.player.hp <= 0 ||
        hit.mana < 0 || hit.stamina < 0 || std::any_of(hit.channels.begin(), hit.channels.end(), [](int64_t n) { return n < 0; }))
        return {DomainStatus::InvalidActor,{}};
    auto next = state_; auto &recovery = next.players[actor.actor]; auto &states = recovery.states;
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    float dealt = 0, absorbed = 0;
    std::optional<SkillCastSpec> shield;
    for (const auto &effect : states.entries()) {
        if (!effect.activeAt(actor.tick) || effect.spec.source.kind != CombatEffectSource::Skill || !p->rules.skills ||
            !p->rules.skills->definitions.contains(effect.spec.source.definition)) continue;
        auto skill = skills::evaluate(*p,effect.spec.source.definition,effect.spec.source.level);
        if (skill.effect == SkillBehavior::EnergyShield) { shield = std::move(skill); break; }
    }
    for (size_t i = 0; i < hit.channels.size(); ++i) {
        if (i == 5 && hit.poisonFrames) continue;
        float raw = float(hit.channels[i]) / 256.f;
        if(i==0 && raw>0) {
            std::vector<RemovedCombatEffect> removed;
            const float remaining=states.absorbPhysical(raw,actor.tick,removed);
            if(remaining!=raw) {
                for(const auto &effect:states.entries()) if(effect.activeAt(actor.tick) && effect.spec.physicalShieldMaximum>0)
                    edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.spec.state.id,true,
                        {{p->rules.skills->nativeStats.at("bonearmor"),effect.spec.physicalShield},
                         {p->rules.skills->nativeStats.at("bonearmormax"),effect.spec.physicalShieldMaximum}}});
                for(const auto &effect:removed) edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
            }
            raw=remaining;
        }
        if (shield) {
            const auto stopped = absorbSkillShield(raw,edit.player.mana,shield->shieldPercent,shield->shieldManaFactor);
            raw = stopped.damage; edit.player.mana = stopped.mana;
        }
        const auto channel = mitigatePlayerDamage(raw,DamageType(i),p->totals.character);
        dealt += channel.dealt; absorbed += channel.absorbed;
    }
    edit.player.hp = std::clamp(edit.player.hp-dealt+absorbed,0.f,float(p->totals.character.maxLife));
    edit.player.mana = std::max(0.f,edit.player.mana-float(hit.mana)/256.f);
    edit.player.stamina = std::max(0.f,edit.player.stamina-float(hit.stamina)/256.f);
    if(edit.player.hp>0 && hit.stunFrames && p->rules.skills && p->rules.skills->stunned.id>=0) {
        const auto &definition=p->rules.skills->stunned;
        if(states.size()>=128 && !states.hasState(definition.id,actor.tick)) return {DomainStatus::Capacity,{}};
        CombatEffectSpec stun;stun.state=definition;stun.duration=std::min<uint64_t>(250,hit.stunFrames);
        stun.source={CombatEffectSource::Monster,source,definition.id,0};
        states.apply(std::move(stun),actor.tick);
    }
    if(edit.player.hp>0 && hit.slowFrames && definitions.slow.id>=0) {
        if(states.size()>=128 && !states.hasState(definitions.slow.id,actor.tick)) return {DomainStatus::Capacity,{}};
        CombatEffectSpec slow;slow.state=definitions.slow;slow.duration=hit.slowFrames;
        slow.source={CombatEffectSource::Monster,source,definitions.slow.id,0};slow.modifiers.velocityPercent=hit.slowPercent;
        states.apply(std::move(slow),actor.tick);
    }
    if (edit.player.hp > 0 && hit.coldFrames && !p->totals.character.combat.cannotBeFrozen && definitions.cold.id >= 0) {
        auto duration = hit.coldFrames * uint64_t(std::clamp(100-p->totals.character.coldResist,0,200)) / 100;
        if (p->totals.character.combat.halfFreezeDuration) duration /= 2;
        for (const auto &effect : states.entries()) if (effect.spec.state.id == definitions.cold.id && effect.expiresAt && *effect.expiresAt > actor.tick)
            duration = std::max(duration,*effect.expiresAt-actor.tick);
        if (duration) {
            if (states.size() >= 128 && !states.hasState(definitions.cold.id,actor.tick)) return {DomainStatus::Capacity,{}};
            CombatEffectSpec cold; cold.state = definitions.cold; cold.duration = duration;
            cold.source = {CombatEffectSource::Monster,source,definitions.cold.id,0};
            cold.modifiers.velocityPercent = cold.modifiers.otherAnimationRate = cold.modifiers.combat.attackRate = -50;
            states.apply(std::move(cold),actor.tick);
        }
    }
    if (edit.player.hp > 0 && hit.poisonFrames && hit.channels[5] && !p->totals.character.combat.preventPoison && definitions.poison.id >= 0) {
        const int64_t rate = int64_t(mitigatePlayerDamage(float(hit.channels[5])/256.f,DamageType::Poison,p->totals.character).dealt*256.f);
        const int lengthResist = std::clamp(p->totals.character.combat.poisonLengthResist+(p->rules.character?p->rules.character->resistancePenalty:0),-100,75);
        const auto duration = hit.poisonFrames * unsigned(100-lengthResist) / 100;
        if (duration && replacesPoison(recovery.poison?recovery.poison->damage.rate:0,rate)) {
            if (states.size() >= 128 && !states.hasState(definitions.poison.id,actor.tick)) return {DomainStatus::Capacity,{}};
            CombatEffectSpec poison; poison.state = definitions.poison; poison.duration = duration;
            poison.source = {CombatEffectSource::Monster,source,definitions.poison.id,0};
            states.apply(std::move(poison),actor.tick);
            recovery.poison = PoisonStatus{{rate,duration,definitions.poison.id},source,actor.tick+duration,actor.tick+1};
        }
    }
    if (edit.player.mana <= 0 && shield) {
        std::vector<EffectHandle> depleted;
        for (const auto &effect : states.entries()) if (effect.spec.source.kind == CombatEffectSource::Skill &&
            effect.spec.source.definition == shield->sourceId) depleted.push_back(effect.handle);
        for (const auto handle : depleted) states.remove(handle);
    }
    edit.transient = projection(states,actor.tick);
    auto random=contactRandom?contactRandom->get():ports_.random;
    const auto clock=reactionTiming(*p,"gh");
    const bool getHit=source && (dealt>0 || hit.stunFrames) && edit.player.hp>0 && !hit.knockback && clock &&
        !ports_.skills.uninterruptible(actor.actor) &&
        monsterHitRecovery(int64_t(dealt*256),int64_t(p->totals.character.maxLife)*256,hit.hitClass,false,
            hit.channels[5]>0 && std::all_of(hit.channels.begin(),hit.channels.begin()+5,[](auto n){return n==0;}),true,random,
            p->rules.skills && states.hasState(p->rules.skills->stunned.id,actor.tick));
    const auto knockbackClock=hit.knockback?reactionTiming(*p,"kb"):std::optional<AnimationTiming>{};
    const bool knockback=source && dealt>0 && edit.player.hp>0 && knockbackClock && !ports_.skills.uninterruptible(actor.actor);
    if(getHit || knockback) recovery.actionUntil=actor.tick+std::min<uint64_t>((knockback?knockbackClock:clock)->durationTicks(),UINT64_MAX-actor.tick);
    const auto percent = playerLifePercentage(int64_t(edit.player.hp*256),int64_t(p->totals.character.maxLife)*256);
    if(dealt>0 || hit.mana>0 || hit.stamina>0 || getHit) edit.publicFacts.emplace_back(HitFact{actor.actor,0,actor.area,percent,edit.player.hp<=0,p->position,hit.hitClass});
    if(getHit || knockback) for(auto &fact:edit.publicFacts) if(auto *damage=std::get_if<HitFact>(&fact)) damage->playerMode=knockback?20:6;
    if(knockback) if(const auto *attacker=ports_.monsters.find(source)) {
        const auto &area=ports_.areas.at(actor.area);const Vec destination=knockbackDestination(p->position,attacker->position,3);
        if(area.definition.collision.nativeMovementSegment(p->position,destination,playerMovement)) {
            edit.knockback=PointTarget{actor.area,area.generation,destination};
            for(auto &fact:edit.publicFacts) if(auto *damage=std::get_if<HitFact>(&fact)) damage->knockback=destination;
        }
    }
    transactions::Change change=std::move(edit);
    if(wear) {
        const auto &resource=std::get<transactions::CharacterEdit>(change);
        transactions::InventoryEdit inventory{actor,p->inventoryRevision,p->characterRevision,p->persistent.inventory,{},p->persistent.player.weaponSet};
        const auto found=inventory.inventory.items.find(wear->id);
        if(found==inventory.inventory.items.end() || found->second.revision!=wear->revision || found->second.revision==UINT64_MAX || !found->second.durability) return {DomainStatus::Stale,{}};
        auto &item=found->second;--item.durability;++item.revision;
        inventory.changes.push_back({item.id,item.revision,ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
        inventory.character=resource.player;inventory.transient=resource.transient;inventory.publicFacts=resource.publicFacts;inventory.knockback=resource.knockback;
        change=std::move(inventory);
    }
    auto itemEvents=prepareItemEvents(actor,{ItemSkillEvent::GetHit,ItemSkillEvent::Death},source,source && ports_.monsters.find(source)?ports_.monsters.find(source)->position:p->position);
    if(!itemEvents) return {itemEvents.status,{}};
    auto plan = ports_.transactions.prepare(std::move(change)); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) {
        state_.players.swap(next.players);
        if(contactRandom) contactRandom->get()=random;else ports_.random=random;
        if(getHit || knockback) ports_.skills.interrupt(actor);
        if(dealt>0) commitItemEvents(std::move(*itemEvents.value),p->persistent.player.hp<=0);
    }
    return {result.status,result?std::optional{dealt}:std::nullopt};
}
void System::react(const ActorContext &actor, EntityId attacker, CombatEffectEvent event, bool returnFire) {
    const auto entry=state_.players.find(actor.actor);if(entry==state_.players.end()) return;
    if(event==CombatEffectEvent::HitByMissile && !returnFire) return;
    for(auto reaction:entry->second.states.reactions(event,actor.tick)) reactions_.push_back({actor,attacker,std::move(reaction)});
}
bool System::missileHitAllowed(EntityId id,uint64_t tick) const {const auto it=state_.players.find(id);return it==state_.players.end() || it->second.nextMissileHit<=tick;}
void System::missileHitDelay(EntityId id,uint64_t until) {state_.players[id].nextMissileHit=until;}

}
