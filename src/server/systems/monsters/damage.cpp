#include "system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/runtime/skill_rules.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "system_detail.hpp"
namespace d2x::server::monsters {
DomainResult<> System::damage(EntityId id, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames, bool freeze, uint8_t hitClass,std::optional<PoisonApplication> poison,bool poisonOnly,uint64_t stunFrames,std::optional<OpenWoundsApplication> wound,int64_t absorbed,bool clearStun,int64_t crushing,int coldPierce,bool softHit,std::optional<uint64_t> freezeLength,std::optional<bool> getHit,DamageStatus status) {
    auto it = state_.actors.find(id);
    const auto suicideModifier=[](const Actor &unit){return std::ranges::find(unit.rule.nativeModifiers,33)!=unit.rule.nativeModifiers.end() ||
        (unit.rule.enchantment && unit.rule.enchantment->has(33));};
    if(it!=state_.actors.end() && !it->second.alive() && suicideModifier(it->second)) return ports_.effects.monsterDamageEffect(id,tick);
    if(it!=state_.actors.end() && !it->second.alive() && (it->second.rule.deathBurst || it->second.rule.boneDeathExplosion || it->second.rule.ai.kind==MonsterAiKind::SiegeBeast)) return ports_.effects.monsterDeathEffect(id,tick);
    if (it == state_.actors.end() || (!it->second.alive() && !it->second.delayedDeathSource) || !it->second.damageable() || crushing < 0) return {DomainStatus::InvalidActor, {}};
    if(it->second.revision>UINT64_MAX-4) return {DomainStatus::Capacity,{}};
    auto actor = it->second;
    if(wound && !actor.rule.stateTarget.acceptsCurseList()) wound.reset();
    // Crushing Blow changes current life before the ordinary damage channel;
    // it must not inflate the hit-recovery damage threshold.
    // The poison rate contributes once to the contact damage as well as to
    // HPREGEN (SUnitDmg total-damage channel). Duration immunity does not erase it.
    if(poison) amount=nativeAdd(nativeChecked(amount),nativeChecked(std::max<int64_t>(0,poison->rate)));
    const auto restoredLife=std::min(actor.maximumLife,actor.life+std::max<int64_t>(0,absorbed));
    const auto afterCrushing=std::max<int64_t>(0,restoredLife-crushing);
    const auto remainingLife=status.actionOnly?actor.life:poisonOnly && !poison?std::max<int64_t>(0,afterCrushing-amount):subtractContactResource(afterCrushing,amount);
    const auto life=std::max(actor.hireling && poisonOnly && !poison && ports_.areas.at(actor.area).definition.town?int64_t(256):int64_t(0),remainingLife);
    const uint8_t percent = monsterLifeRatio(life,actor.maximumLife);
    auto random=actor.combatRandom;
    auto modifiers=ports_.effects.unitModifiers(id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    auto chilled=actor.chilledUntil, frozen=actor.frozenUntil;
    auto shatterActive=actor.shatterActive;
    const auto states=ports_.effects.unitStates(id,tick);
    const bool uninterruptable=states.contains(actor.rule.uninterruptableState) || actor.aiStates.contains(actor.rule.uninterruptableState);
    if(actor.delayedDeathSource && (amount || uninterruptable)) return {DomainStatus::Unavailable,{}};
    const bool delayDeath=!life && uninterruptable;
    const bool dead=!life && !delayDeath && !status.deferAction;
    reactions::ItemEventPlan itemEvents;
    if(!status.suppressEvents && ports_.skillPrograms && ports_.skillPrograms->pd2 && (dead || (!status.actionOnly && !poisonOnly))) {
        const combat::Participants participants{ports_.players,*this,ports_.social};
        const auto attacker=participants.find(status.source?status.source:actor.lastAttacker);
        const ActorContext context{actor.owner.value_or(PlayerId{}),actor.id,actor.area,ports_.areas.at(actor.area).generation,0,tick};
        auto events=ports_.itemTriggers.prepare(context,status.actionOnly || poisonOnly?std::initializer_list{ItemSkillEvent::Death}:
            std::initializer_list{ItemSkillEvent::GetHit,ItemSkillEvent::Death},attacker.id(),attacker.id()?attacker.position():actor.position);
        if(!events) return {events.status,{}};
        itemEvents=std::move(*events.value);
    }
    const bool freezable=!actor.hireling && !actor.rule.boss && !nativeUniqueMonster(actor.identity.rank);
    coldFrames=coldHitDuration(coldFrames,ports_.effects.unitResistance(id,DamageType::Cold,tick,coldPierce),0,
        modifiers.cannotBeFrozen,modifiers.halfFreezeDuration);
    const auto rawFreeze=freezeLength?coldHitDuration(*freezeLength,ports_.effects.unitResistance(id,DamageType::Cold,tick,coldPierce),0,
        modifiers.cannotBeFrozen,modifiers.halfFreezeDuration):freeze?coldFrames:0;
    bool appliedFreeze=false,coldApplied=false;std::optional<uint64_t> freezeThinkAt;
    const auto applyCold=[&](uint64_t frames) {
        if(!frames || !actor.rule.coldEffect) return;
        coldApplied=true;
        const auto duration=std::max<uint64_t>(1,actor.rule.coldEffect<0?frames/unsigned(actor.rule.coldDivisor):frames);
        chilled=std::max(chilled,tick+std::min(duration,UINT64_MAX-tick));
        // Retail 1.13c RVA DC8F8, unlike the reconstructed reversed condition.
        shatterActive=limitedRandom(random,100)<20 && !actor.rule.deadCollision;
    };
    if(freezeLength || !freeze) applyCold(coldFrames);
    if(rawFreeze && !uninterruptable) {
        if(freezable && actor.rule.coldEffect<0) {
            appliedFreeze=true;
            const auto duration=rawFreeze/unsigned(actor.rule.freezeDivisor);
            frozen=std::max(frozen,tick+std::min(duration,UINT64_MAX-tick));
            freezeThinkAt=tick+std::min(duration+1,UINT64_MAX-tick);
        } else if(!freezable) applyCold(rawFreeze);
    }
    const auto receivedCold=coldApplied && chilled>actor.chilledUntil?chilled:0;
    const auto receivedFreeze=appliedFreeze?frozen:0;
    auto deathStates=actor.corpse.states;
    bool corpseUnavailable=actor.corpse.unavailable;
    if(dead) {
        const auto retain=[&](const CombatStateDefinition &state,bool enabled) {
            const auto kind=actor.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
            if(enabled && state.id>=0 && state.stayOnDeath[size_t(kind)]) {
                deathStates.insert(state.id);
                corpseUnavailable|=state.hideOnDeath || state.shatterOnDeath || state.corpseUnselectable;
            }
        };
        retain(actor.rule.frozenState,appliedFreeze || frozen>tick);
        retain(actor.rule.shatterState,shatterActive ||
            (actor.rule.enchantment && actor.rule.enchantment->has(35)));
        chilled=frozen=0;shatterActive=false;
    }
    auto poisoned=actor.poison;bool appliedPoison=false;
    if(poison) {
        const int lengthResistance=actor.hireling?
            std::clamp(modifiers.poisonLengthResist-poison->lengthPierce+actor.rule.difficultyResistancePenalty,-100,75):
            piercedResistance(modifiers.poisonLengthResist,poison->lengthPierce);
        poison->frames=modifiers.preventPoison?0:resistedDuration(poison->frames,lengthResistance);
        if(life && poison->frames && poison->state>=0 && replacesPoison(poisoned?poisoned->damage.rate:0,poison->rate)) {
            appliedPoison=true;
            const auto until=tick+std::min(poison->frames,UINT64_MAX-tick);
            if(poisoned) {
                // Native refresh changes rate/deadline, retaining the stat-list owner.
                poisoned->damage.rate=poison->rate;poisoned->damage.frames=poison->frames;poisoned->until=until;
                poisoned->nativeExpireFrame.reset();poisoned->removalDue.reset();
            } else poisoned=PoisonStatus{*poison,source,until,tick+1};
        }
    }
    if(dead) poisoned.reset();
    auto stunned=actor.stunnedUntil;bool appliedStun=false;
    if(clearStun) stunned=0;
    const bool unique=nativeUniqueMonster(actor.identity.rank);
    if(stunFrames && unique && !status.stunApplied) throw std::logic_error("Unique stun requires its actual attacker seed decision");
    if(life && stunFrames && actor.rule.stunState>=0 && status.stunApplied.value_or(!unique && !actor.rule.boss && actor.rule.nativeVelocity!=0)) {appliedStun=true;stunned=tick+std::min<uint64_t>(actor.hireling?13:250,stunFrames);}
    if(dead) stunned=0;
    const bool recovery=!status.deferAction && life && !poisonOnly && getHit.value_or(!softHit) && monsterHitRecovery(amount,actor.maximumLife,hitClass,appliedFreeze || states.contains(actor.rule.frozenState.id),poisonOnly,actor.rule.hitRecoveryTicks>0,random,stunned>tick);
    const int reactionTicks=recovery?recoveryTicks(actor,ports_.effects,tick,false):0;
    std::vector<DomainFact> facts;
    const bool wasShatter=actor.shatterActive;
    const bool shatter=shatterActive || deathStates.contains(actor.rule.shatterState.id);
    if((shatter!=wasShatter || (!life && shatter)) && actor.rule.shatterState.id>=0) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,shatter});
    // Publish original states before the death action, including a lethal first freeze.
    facts.emplace_back(HitFact{id,1,actor.area,percent,dead,actor.position,hitClass,uint8_t(recovery?3:0),{},actor.lightningReady});
    actor.life = life;
    if(status.deferAction && !life) actor.modeAlive=true;
    if(delayDeath) {actor.modeAlive=true;actor.delayedDeathSource=status.source?status.source:source;actor.killer=source;}
    else if(dead) {actor.modeAlive=false;actor.delayedDeathSource.reset();}
    refreshCollision(actor);
    if(dead && actor.petRegistrationOrder) actor.inactivePet=true;
    ++actor.revision; if(amount>0 && !poisonOnly && !status.actionOnly) ++actor.damageOccurrence;
    actor.combatRandom=random;
    if (recovery) ++actor.hitOccurrence;
    if(!status.deferAction && !poisonOnly) {
        if(dead) recordNativeMode(actor,0);
        else if(recovery) recordNativeMode(actor,3);
        else if(softHit || getHit.value_or(false)) actor.nativeAiState=19;
    }
    actor.chilledUntil = chilled; actor.frozenUntil = frozen;
    actor.shatterActive=shatterActive;actor.corpse.states=std::move(deathStates);
    actor.stunnedUntil=stunned;
    actor.poison=poisoned;
    const bool appliedWound=life && bool(wound);
    if(dead) actor.wound.reset();
    else if(wound) {
        // Native same-state/same-skill refresh extends the existing stat list;
        // its original rate and source survive instead of stacking wounds.
        if(wound->pd2Stacks) actor.wound=std::move(wound);
        else if(actor.wound) actor.wound->until=wound->until;
        else actor.wound=std::move(wound);
    }
    if(recovery || frozen>tick || stunned>tick || dead) {
        ++actor.interruption;actor.route.clear();actor.moving=actor.running=false;
        if(recovery) {actor.reactionMode=3;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;}
    }
    std::vector<FrameEventQueue::PreparedInsertion> cascades;
    if (dead) {
        actor.itemAuraTimers.clear();
        // Fn043 marks an exhausted nest NOTC before its own death action.
        if(source==id && (actor.rule.ai.kind==MonsterAiKind::FoulCrowNest || actor.rule.ai.kind==MonsterAiKind::Sarcophagus)) actor.rule.noTreasure=true;
        actor.corpse.unavailable=corpseUnavailable;
        actor.deathTick = tick; actor.deathOccurrence = 0; actor.killer = source;
        actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; actor.busyUntil = tick + uint64_t(actor.rule.deathTicks);
        cascades=prepareDeathSweep(actor,tick);
    }
    effects::UnitDamageStates updates{status.source?status.source:source,receivedCold,receivedFreeze,appliedStun?stunned:0,status.burn,status.burnFrames};
    updates.clearStun=clearStun;updates.poisonApplied=appliedPoison;updates.woundApplied=appliedWound;
    updates.freezeThinkAt=freezeThinkAt;updates.deathDelay=delayDeath;
    updates.resetRegeneration=life && poison && poison->frames && poison->state>=0;updates.contact=amount>0 && !status.actionOnly;
    auto publication=ports_.effects.prepareDamageStates(std::move(actor),std::move(updates),tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Conflict,{}};
    facts.insert(facts.begin(),std::make_move_iterator(publication.facts.begin()),std::make_move_iterator(publication.facts.end()));
    auto event=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},it->second.area},std::move(facts)});
    if(!event) return {event.status,{}};
    if(dead) publication.actors->actors.at(id).deathOccurrence=*event.value;
    ports_.effects.publishAuraUnits(publication);
    ports_.itemTriggers.commit(std::move(itemEvents),dead);
    if(dead) {
        ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills);
        ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicStats);
    }
    for(auto &cascade:cascades) ports_.frameEvents.publish(std::move(cascade));
    if(const auto *published=find(id);!dead && published && suicideModifier(*published)) return ports_.effects.monsterDamageEffect(id,tick);
    return dead?ports_.effects.monsterDeathEffect(id,tick):DomainResult<>{DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::finishDamageAction(EntityId id,EntityId source,int64_t amount,uint64_t tick,uint8_t hitClass,bool softHit,std::optional<bool> getHit) {
    const auto *unit=find(id);if(!unit || !unit->alive() || unit->delayedDeathSource) return {DomainStatus::Applied,std::monostate{}};
    DamageStatus status;status.source=source;status.actionOnly=true;
    return damage(id,source,amount,tick,0,false,hitClass,{},false,0,{},0,false,0,0,softHit,{},getHit,status);
}
DomainResult<> System::block(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(actor.rule.blockTicks>0) {
        const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
        const int reactionTicks=recoveryTicks(actor,ports_.effects,tick,true);
        const uint8_t percent=monsterLifeRatio(actor.life,actor.maximumLife);
        HitFact hit{id,1,actor.area,percent,false,actor.position,0,6};
        if(actor.movementTarget && nativePathTarget(ports_.players,*this,actor.movementTarget,actor.area))
            hit.actionTarget=MotionTarget{actor.movementTarget,uint8_t(ports_.players.findActor(actor.movementTarget)?0:1)};
        auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{hit}});
        if(!result) return {result.status,{}};
        stop(id);recordNativeMode(actor,6);++actor.interruption;actor.reactionMode=6;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;++actor.revision;
    } else {actor.nativeAiState=19;++actor.revision;}
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::holyFreeze(EntityId id,uint64_t until,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;if(actor.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto random=actor.combatRandom;const bool active=rollRandom(random)%100<20;
    std::vector<DomainFact> facts;
    if(active!=actor.shatterActive && !actor.rule.shatterState.noSend)
        facts.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,active});
    auto deathStates=actor.corpse.states;
    if(!actor.alive()) {
        if(active) deathStates.insert(actor.rule.shatterState.id);
        else deathStates.erase(actor.rule.shatterState.id);
    }
    if(!facts.empty() && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    actor.holyFreezeUntil=until;actor.shatterActive=active;actor.combatRandom=random;actor.corpse.states.swap(deathStates);
    if(!actor.alive() && active) actor.corpse.unavailable|=actor.rule.shatterState.shatterOnDeath || actor.rule.shatterState.corpseUnselectable || actor.rule.shatterState.hideOnDeath;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::hitDelay(EntityId id, uint64_t until) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.nextHitTick = std::max(it->second.nextHitTick, until); }
void System::knockback(EntityId id, Vec source, uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || it->second.owner || it->second.rule.knockbackTicks<=0) return;
    auto &m=it->second;stop(id);m.knockbackSource=source;m.knockbackGoal=knockbackDestination(m.position,source,3);
    m.knockedUntil=tick+uint64_t(m.rule.knockbackTicks);m.busyUntil=m.knockedUntil;m.neutralAt.reset();++m.hitOccurrence;++m.interruption;++m.revision;
}
DomainResult<> System::missileReaction(EntityId id,EntityId source,bool getHit,bool knockback,uint8_t hitClass,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::Unavailable,{}};
    const auto &current=found->second;const auto states=ports_.effects.unitStates(id,tick);
    if(!current.alive() || states.contains(current.rule.uninterruptableState)) return {DomainStatus::Applied,std::monostate{}};
    auto actor=current;
    if(knockback && actor.rule.knockbackTicks<=0) {knockback=false;getHit=true;}
    const bool stunned=states.contains(actor.rule.stunState);
    if(!knockback && (!getHit || !stunned || actor.rule.hitRecoveryTicks<=0)) {
        if(getHit) {found->second.nativeAiState=19;++found->second.revision;}
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(actor.revision==UINT64_MAX || actor.interruption==UINT64_MAX || actor.hitOccurrence==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto duration=knockback?actor.rule.knockbackTicks:recoveryTicks(actor,ports_.effects,tick,false);
    actor.route.clear();actor.moving=false;actor.movementTarget={};
    recordNativeMode(actor,knockback?13:3);
    actor.reactionMode=knockback?8:3;actor.reactionUntil=tick+std::min<uint64_t>(uint64_t(duration),UINT64_MAX-tick);
    actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;
    ++actor.interruption;++actor.revision;++actor.hitOccurrence;
    HitFact fact{id,1,actor.area,monsterLifeRatio(actor.life,actor.maximumLife),false,actor.position,hitClass,actor.reactionMode};
    if(knockback) {
        const auto attacker=combat::Participants{ports_.players,*this,ports_.social}.find(source);
        if(!attacker.id()) return {DomainStatus::Unavailable,{}};
        const auto region=attacker.player?attacker.player->area:attacker.monster->area;
        const auto point=attacker.position()+ports_.areas.at(region).definition.origin-ports_.areas.at(actor.area).definition.origin;
        actor.knockbackSource=point;actor.knockbackGoal=knockbackDestination(actor.position,point,3);
        actor.knockedUntil=actor.reactionUntil;fact.knockback=actor.knockbackGoal;
    }
    auto publication=prepareActorPublication({{id,std::move(actor)}},0);
    if(!validateActors(publication)) return {DomainStatus::Conflict,{}};
    const auto output=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},current.area},{fact}});
    if(!output) return {output.status,{}};
    publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
}
void System::rewardComplete(EntityId id) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.rewardComplete = true; }
bool System::canRemove(EntityId id) const {
    const auto *unit=find(id);if(!unit || (!unit->owner && !unit->hydra() && unit->rule.ai.kind!=MonsterAiKind::BaalToStairs && (unit->hostile || unit->rule.ai.kind!=MonsterAiKind::ScriptedNpc))) return false;
    return !ports_.frameEvents.hasHandler({FrameEventUnit::Monster,id},effects::monsterModifierTimerHandler);
}
DomainResult<> System::remove(EntityId id) {
    const auto *unit=find(id);if(!unit || (!unit->owner && !unit->hydra() && unit->rule.ai.kind!=MonsterAiKind::BaalToStairs && (unit->hostile || unit->rule.ai.kind!=MonsterAiKind::ScriptedNpc))) return {DomainStatus::InvalidActor,{}};
    if(!canRemove(id)) return {DomainStatus::Unavailable,{}};
    const auto it=state_.actors.find(id);
    stopScriptedMotion(id);
    ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});ports_.areas.removeRoomUnit(id);state_.actors.erase(it);return {DomainStatus::Applied,std::monostate{}};
}
} // namespace d2x::server::monsters
