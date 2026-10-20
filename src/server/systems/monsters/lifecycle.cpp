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
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "system_detail.hpp"
namespace d2x::server::monsters {
void System::maximumLife(EntityId id,int64_t value) {
    if(value<=0 || value>INT32_MAX) throw std::runtime_error("Monster maximum life outside native signed range");
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || found->second.maximumLife==value) return;
    auto &unit=found->second;
    const auto before=Fixed8::fromRaw(nativeChecked(unit.maximumLife)),after=Fixed8::fromRaw(nativeChecked(value));
    const bool callback=unit.life>0 && before>0;
    unit.life=resourceOnMaximumChange(Fixed8::fromRaw(nativeChecked(unit.life)),before,after).raw();
    unit.maximumLife=value;
    if(callback && unit.nativeStats && unit.rule.stats && unit.rule.damageRegen) {
        auto root=std::make_shared<StatList>(*unit.nativeStats->sources);
        setNativeStat(*root,*unit.rule.stats,"hpregen",monsterRegenerationOnMaximumChange(after,unit.rule.damageRegen));
        unit.nativeStats->values=evaluateStatList(*unit.rule.stats,*root,{root.get(),{}, {}});
        unit.nativeStats->sources=std::move(root);
    }
    ++unit.revision;
}
bool System::canCommitLife(const LifeChange &change) const {
    const auto *unit=find(change.actor);
    if(!unit) return false;
    if(change.nativeMinions && !change.changed) return false;
    if(change.random && (unit->combatRandom!=change.random->first || !change.changed)) return false;
    if(change.itemAuraTimers && !change.periodicEvents && (!change.itemAuraTimers->empty() ||
        ports_.frameEvents.hasEvent({FrameEventUnit::Monster,change.actor},FrameEventType::PeriodicStats))) return false;
    if(change.periodicEvents && (change.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Monster,change.actor} ||
        !ports_.frameEvents.validate(change.periodicEvents->edit))) return false;
    if(change.specialTargetNode.value_or(false) && !unit->specialTargetOrder && state_.nextSpecialTargetOrder==UINT64_MAX) return false;
    if((change.curseControl || change.reselectAi) && unit->aiFunctionRevision==UINT64_MAX) return false;
    if((change.tauntPostActions || change.restoreNeutral || change.aiThinkAt) && unit->aiThinkRevision==UINT64_MAX) return false;
    const bool alignmentChanged=change.intrinsic && unit->rule.stats &&
        change.intrinsic->values.get({unit->rule.stats->names.at("alignment"),0})!=int(unit->alignment());
    if((alignmentChanged || change.curseControl || change.attraction || change.detachedMinion) && unit->allegianceRevision==UINT64_MAX) return false;
    return unit && unit->revision==change.expectedRevision && (!(change.cureColdPoison || change.shatter || change.poison || change.itemAuraTimers || change.curseControl || change.stopMovement || change.hitDelay || change.attraction || change.aiResetTimers || change.specialTargetNode || change.detachedMinion || change.tauntPostActions || change.reselectAi || change.chilledUntil || change.frozenUntil || change.stunnedUntil || change.restoreNeutral || change.wound || change.aiThinkAt) || change.changed) && (!change.changed || unit->revision<UINT64_MAX) &&
        change.maximum>=0 && change.life>=0 && change.life<=change.maximum;
}
DomainResult<> System::consumeRandom(EntityId id,uint64_t expected,uint64_t next) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.combatRandom!=expected) return {DomainStatus::Stale,{}};
    if(expected!=next) {
        if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        unit.combatRandom=next;++unit.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::commitLife(LifeChange &&change) noexcept {
    if(!canCommitLife(change)) return false;
    auto &unit=state_.actors.at(change.actor);
    if(change.changed) {
        const auto alignment=unit.alignment();
        unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();unit.nativeStats=std::move(change.intrinsic);++unit.revision;
        if(change.random) unit.combatRandom=change.random->second;
        if(unit.petRegistrationOrder && !unit.life && !unit.modeAlive) unit.inactivePet=true;
        if(change.cureColdPoison) {unit.poison.reset();unit.chilledUntil=unit.frozenUntil=0;unit.shatterActive=false;}
        if(change.shatter) unit.shatterActive=*change.shatter;
        if(change.poison) unit.poison=*change.poison;
        if(change.wound) unit.wound=*change.wound;
        if(change.chilledUntil) unit.chilledUntil=*change.chilledUntil;
        if(change.frozenUntil) unit.frozenUntil=*change.frozenUntil;
        if(change.stunnedUntil) unit.stunnedUntil=*change.stunnedUntil;
        if(change.restoreNeutral) {
            unit.route.clear();unit.moving=unit.running=false;unit.busyUntil=unit.reactionUntil=0;unit.neutralAt.reset();
        }
        if(change.itemAuraTimers) unit.itemAuraTimers=std::move(*change.itemAuraTimers);
        if(change.periodicEvents) ports_.frameEvents.publish(std::move(change.periodicEvents->edit));
        if(change.curseControl) {
            const auto before=unit.curseControl.ai,after=change.curseControl->ai;
            if(change.reselectAi || (before!=after && before!=CurseAi::Confuse && after!=CurseAi::Confuse && before!=CurseAi::Attract && after!=CurseAi::Attract)) {
                ++unit.aiFunctionRevision;unit.aiThinkAt=0;unit.whippedAi=false;
            }
            unit.curseControl=*change.curseControl;
        }
        if(change.tauntPostActions) unit.mainOwnerCleared=true;
        if(change.restoreNeutral || change.tauntPostActions || change.aiThinkAt) {
            ++unit.aiThinkRevision;unit.aiThinkAt=change.aiThinkAt.value_or(0);
            scheduleAi(unit.id,unit.aiThinkAt);
        }
        if(change.tauntTarget) unit.movementTarget=unit.tauntTarget=*change.tauntTarget;
        if(change.detachedMinion) {
            unit.detachedMinion=*change.detachedMinion;
            if(*change.detachedMinion) unit.nativeMinionOwner=EntityId{};
        }
        if(change.nativeMinions) unit.nativeMinions=std::move(*change.nativeMinions);
        if(change.specialTargetNode) {
            if(!*change.specialTargetNode) unit.specialTargetOrder=0;
            else if(!unit.specialTargetOrder && !unit.owner) unit.specialTargetOrder=state_.nextSpecialTargetOrder++;
        }
        if(change.stopMovement) {unit.route.clear();unit.moving=false;unit.running=false;}
        if(change.hitDelay) unit.nextHitTick=std::max(unit.nextHitTick,*change.hitDelay);
        if(change.attraction) {unit.attractionTarget=change.attraction->target;unit.attractionUntil=change.attraction->until;}
        if(change.aiResetTimers) {unit.aiResetTimers=std::move(*change.aiResetTimers);unit.nativeAiResetTracked=true;}
        if(alignment!=unit.alignment() || change.curseControl || change.attraction || change.detachedMinion) ++unit.allegianceRevision;
    }
    return true;
}
std::vector<FrameEventQueue::PreparedInsertion> System::prepareDeathSweep(Actor &actor,uint64_t tick) const {
    std::vector<FrameEventQueue::PreparedInsertion> events;
    if(!actor.rule.deathSweep) return events;
    const auto &sweep=*actor.rule.deathSweep;auto random=actor.combatRandom;
    const auto point=actor.position+ports_.areas.at(actor.area).definition.origin;
    const auto targets=ports_.effects.nativeAuraCandidateRecipients({actor.id,actor.area,sweep.radius,0x583,false,
        point,effects::NativeAuraTargetMode::TargetRange,tick,point});
    for(const auto key:targets) if(const auto *other=find(key);key!=actor.id && other && other->alive() &&
        other->alignment()==UnitAlignment::Evil && (!sweep.undeadOnly || other->rule.undead)) {
        const auto delay=nativeAdd(sweep.minimumDelay,int32_t(limitedRandom(random,uint32_t(nativeSubtract(sweep.maximumDelay,sweep.minimumDelay)))));
        events.push_back(ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,key},FrameEventType::MonsterModifier,
            tick+uint64_t(uint32_t(delay)),false,0,21}));
    }
    actor.combatRandom=random;return events;
}
DomainResult<Actor> System::prepareDeathActor(EntityId id,uint64_t tick) const {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    return prepareDeathActor(found->second,tick);
}
DomainResult<Actor> System::prepareDeathActor(const Actor &current,uint64_t tick) const {
    const auto id=current.id;if(!current.alive()) return {DomainStatus::Applied,current};
    if(current.revision>UINT64_MAX-4 || current.interruption==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto unit=current;unit.life=0;unit.modeAlive=false;unit.deathTick=tick;unit.deathOccurrence=0;
    if(unit.petRegistrationOrder) unit.inactivePet=true;
    if(unit.questNpc || unit.rule.environmentController) unit.rewardComplete=true;
    unit.itemAuraTimers.clear();unit.poison.reset();unit.wound.reset();
    unit.busyUntil=tick+uint64_t(unit.rule.deathTicks);unit.route.clear();unit.moving=unit.running=false;
    unit.movementTarget={};unit.aiPose=0;unit.holdPose=false;unit.poseUntil=unit.riseUntil=unit.reactionUntil=0;
    unit.chilledUntil=unit.frozenUntil=unit.stunnedUntil=0;unit.shatterActive=false;unit.delayedDeathSource.reset();
    const auto kind=unit.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
    unit.corpse.states.clear();
    if(unit.rule.states) for(const auto state:ports_.effects.unitStates(id,tick)) {
        const auto definition=unit.rule.states->find(state);
        if(definition!=unit.rule.states->end() && definition->second.stayOnDeath[size_t(kind)]) {
            unit.corpse.states.insert(state);
            unit.corpse.unavailable|=definition->second.hideOnDeath || definition->second.shatterOnDeath || definition->second.corpseUnselectable;
        }
    }
    unit.aiStates.clear();unit.spawnReservation.reset();++unit.interruption;++unit.revision;
    return {DomainStatus::Applied,std::move(unit)};
}
std::vector<Actor> System::prepareMinionDeaths(Actor candidate,uint64_t tick,const std::map<EntityId,Actor> *staged) const {
    std::map<EntityId,Actor> changed;std::vector<EntityId> order;
    const auto first=candidate.id;changed.emplace(first,std::move(candidate));order.push_back(first);
    const auto edit=[&](EntityId id)->Actor * {
        if(const auto found=changed.find(id);found!=changed.end()) return &found->second;
        const auto *source=find(id);
        if(staged) if(const auto found=staged->find(id);found!=staged->end()) source=&found->second;
        if(!source) return nullptr;
        if(source->revision==UINT64_MAX) throw std::overflow_error("Native minion owner revision exhausted");
        auto copy=*source;++copy.revision;order.push_back(id);return &changed.emplace(id,std::move(copy)).first->second;
    };
    std::vector<EntityId> pending{first};
    for(size_t cursor=0;cursor<pending.size();++cursor) {
        auto &unit=changed.at(pending[cursor]);
        if(unit.alive()) continue;
        if(unit.ownsNativeMinions && !unit.ownershipDeathHandled) {
            unit.ownershipDeathHandled=true;
            if(!unit.transferMinionsOnDeath) {
                for(const auto id:unit.nativeMinions)
                    if(auto *child=edit(id)) child->nativeMinionOwner=EntityId{};
            } else {
                for(const auto id:unit.nativeMinions) if(id!=unit.id) if(auto *successor=edit(id)) {
                    successor->nativeMinionOwner=id;successor->ownsNativeMinions=true;
                    successor->transferMinionsOnDeath=true;unit.nativeMinionOwner=id;
                    successor->nativeMinions.insert(successor->nativeMinions.begin(),unit.id);
                    for(const auto other:unit.nativeMinions) if(other!=id) if(auto *child=edit(other)) {
                        child->nativeMinionOwner=id;
                        successor->nativeMinions.insert(successor->nativeMinions.begin(),other);
                    }
                    break;
                }
            }
        }
        if(!unit.killMinionsOnDeath || unit.minionDeathHandled) continue;
        unit.minionDeathHandled=true;
        const auto owner=unit.nativeMinionOwner.value_or(minionOwner(unit.id));
        if(!owner || (!find(owner) && !ports_.players.findActor(owner))) continue;
        const auto minions=unit.nativeMinions;unit.nativeMinions.clear();
        for(const auto id:minions) if(auto *child=edit(id)) {
            // FreeMinionList + clear owner precede ModeDeath. Its UMod15
            // callback consequently cannot recurse through the old parent.
            child->nativeMinionOwner=EntityId{};
            if(child->alive()) {
                auto dead=prepareDeathActor(*child,tick);
                if(!dead) throw std::overflow_error("Native minion death revision exhausted");
                *child=std::move(*dead.value);pending.push_back(id);
            }
        }
        if(auto *parent=edit(owner)) {
            parent->nativeMinionOwner=EntityId{};
            if(owner!=unit.id && parent->alive()) {
                auto dead=prepareDeathActor(*parent,tick);
                if(!dead) throw std::overflow_error("Native minion parent death revision exhausted");
                *parent=std::move(*dead.value);pending.push_back(owner);
            }
        }
    }
    std::vector<Actor> result;result.reserve(order.size());
    for(const auto id:order) result.push_back(std::move(changed.at(id)));
    return result;
}
DomainResult<> System::scriptDeath(EntityId id,uint64_t tick,EntityId source) {
    const auto *live=find(id);if(!live) return {DomainStatus::InvalidActor,{}};
    if(!live->alive()) return ports_.effects.monsterDeathEffect(id,tick);
    const auto &current=*live;
    auto prepared=prepareDeathActor(id,tick);if(!prepared) return {prepared.status,{}};
    auto unit=std::move(*prepared.value);if(source) unit.killer=source;
    auto cascades=prepareDeathSweep(unit,tick);
    auto publication=ports_.effects.prepareDamageStates(std::move(unit),{},tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    publication.facts.emplace_back(HitFact{id,1,current.area,0,true,current.position,0,0});
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},current.area},std::move(publication.facts)});
    if(!sent) return {sent.status,{}};
    publication.actors->actors.at(id).deathOccurrence=*sent.value;
    ports_.effects.publishAuraUnits(publication);
    ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills);
    ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::PeriodicStats);
    for(auto &cascade:cascades) ports_.frameEvents.publish(std::move(cascade));
    return ports_.effects.monsterDeathEffect(id,tick);
}
DomainResult<> System::finishTrap(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.rule.environmentController) return {DomainStatus::InvalidActor,{}};
    return scriptDeath(id,tick);
}
DomainResult<> System::reduceShrineLife(EntityId id,int64_t amount) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.damageable() || found->second.life<=0 || amount<0 || amount>=found->second.life) return {DomainStatus::InvalidActor,{}};
    found->second.life-=amount;++found->second.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::upgradeShrine(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::Stale,{}};
    auto &actor=found->second;
    if(modifierPending_) return {DomainStatus::Conflict,{}};
    if(!actor.enemyTarget() || actor.life<=0 || actor.identity.rank!=MonsterRank::Normal || !actor.rule.shrineEligible || !actor.rule.modifierChoices ||
        (actor.moving && actor.running) || actor.busyUntil>tick || actor.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    if(nextModifierToken_==UINT64_MAX) return {DomainStatus::Capacity,{}};
    ModifierPreparation request;request.token=nextModifierToken_;request.tick=tick;request.area=actor.area;
    request.generation=ports_.areas.at(actor.area).generation;request.expectedRandom=request.nextRandom=actor.combatRandom;
    request.leader={id,actor.revision,actor.identity,actor.rule.level,actor.rule.nativeClass};
    const auto &choices=*actor.rule.modifierChoices;
    const bool champion=limitedRandom(request.nextRandom,100)<uint32_t(choices.championChance);
    request.leader.identity.rank=champion?MonsterRank::Champion:MonsterRank::Unique;
    const int count=champion?1:actor.rule.difficulty+int(limitedRandom(request.nextRandom,1))+1;
    request.selection.ids=chooseMonsterModifiers(choices.candidates[champion?1:0],actor.rule.nativeModifiers,count,request.nextRandom);
    std::vector<EntityId> minions;
    for(const auto otherId:actor.nativeMinions) {
        const auto *minion=find(otherId);
        if(!minion || otherId==id || minion->area!=actor.area || minion->life<=0 || !minion->enemyTarget() || minionOwner(otherId)!=id) continue;
        minions.push_back(otherId);
    }
    return prepareBirthModifiers(id,request.leader.identity.rank,std::move(request.selection.ids),minions,tick,
        std::pair{request.expectedRandom,request.nextRandom});
}
} // namespace d2x::server::monsters
