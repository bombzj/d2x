#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/area_store.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace d2x::server::monsters {
void System::prepareInitialTimers(Actor &unit,uint64_t tick) const {
    const FrameEventOwner owner{FrameEventUnit::Monster,unit.id};
    if(!unit.nativeAi()) return;
    if(!unit.initialAiTimer && (unit.alive() || unit.owner)) unit.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion({{},owner,FrameEventType::AiThink,tick+1}));
    if(!unit.initialModeTimer) unit.initialModeTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion({{},owner,FrameEventType::ModeChange,tick+1,true}));
}
void System::publishInitialAi(Actor &unit) noexcept {
    for(auto &timer:unit.initialModifierTimers) ports_.frameEvents.publish(std::move(*timer));
    unit.initialModifierTimers.clear();
    if(unit.initialWakeTimer) {
        ports_.frameEvents.publish(std::move(*unit.initialWakeTimer));unit.initialWakeTimer.reset();
    }
    if(unit.initialModeTimer && unit.initialModeTimer->handle()) {
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,unit.id},FrameEventType::ModeChange,0);
        ports_.frameEvents.publish(std::move(*unit.initialModeTimer));unit.initialModeTimer.reset();
    }
    if(unit.initialAiTimer && unit.initialAiTimer->handle()) {
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,unit.id},FrameEventType::AiThink,0);
        ports_.frameEvents.publish(std::move(*unit.initialAiTimer));
    }
    unit.initialAiTimer.reset();
}
void System::resumeMode(EntityId id,uint64_t tick) {
    const auto *unit=find(id);if(!unit || !unit->nativeAi()) return;
    const FrameEventOwner owner{FrameEventUnit::Monster,id};
    if(ports_.frameEvents.hasEvent(owner,FrameEventType::ModeChange)) return;
    ports_.frameEvents.insertPrepared({{},owner,FrameEventType::ModeChange,tick+1,true});
}
void System::roomClientsChanged(RoomView room,bool present,uint64_t tick) {
    std::vector<ActorPublication> publications;
    for(const auto id:ports_.areas.roomUnitOrder(room)) {
        const auto *unit=find(id);if(!unit || !unit->nativeAi()) continue;
        if(unit->revision==UINT64_MAX || unit->aiThinkRevision==UINT64_MAX)
            throw std::overflow_error("Monster room client revision exhausted");
        auto candidate=*unit;candidate.initialAiTimer.reset();
        const bool neutral=!candidate.moving && candidate.route.empty() && candidate.busyUntil<=tick &&
            candidate.riseUntil<=tick && candidate.reactionUntil<=tick && candidate.aiPose==1;
        if(present && candidate.alive() && (neutral || candidate.rule.wakeAiOutsideNeutral)) {
            candidate.aiThinkAt=tick+2;++candidate.aiThinkRevision;
            candidate.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,id},FrameEventType::AiThink,tick+2}));
        }
        ++candidate.revision;
        publications.push_back(prepareActorPublication({{id,std::move(candidate)}},0));
    }
    for(auto &publication:publications) {
        const auto id=publication.actors.begin()->first;
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,id},FrameEventType::AiThink,0);
        if(!present) ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,id},FrameEventType::StatRegeneration,0);
        publishActors(publication);
    }
}
DomainResult<> System::modeEvent(const FrameEvent &event,uint64_t tick) {
    if(tick<event.due) return {DomainStatus::Applied,std::monostate{}};
    if(advanceMode(event.owner.unit,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    const auto *unit=find(event.owner.unit);
    if(unit && unit->rule.deathKillsStoredOwner && !unit->alive() && tick>=unit->busyUntil && find(unit->nativeOwner)) {
        const auto killed=scriptDeath(unit->nativeOwner,tick);if(!killed) return killed;
    }
    if(unit && unit->hydra() && !unit->owner && !unit->alive() && tick>=unit->busyUntil) return remove(unit->id);
    if(unit && !unit->alive() && unit->rule.deathReinitialize && tick>=unit->busyUntil) {
        if(!unit->rewardComplete) return {DomainStatus::Applied,std::monostate{}};
        return reinitializeAfterDeath(unit->id,tick);
    }
    if(!unit || (!unit->alive() && !unit->delayedDeathSource && tick>=unit->busyUntil)) ports_.frameEvents.cancel(event.handle);
    return {DomainStatus::Applied,std::monostate{}};
}
void System::scheduleAi(EntityId id,uint64_t due) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return;
    const auto &unit=found->second;
    const FrameEventOwner owner{FrameEventUnit::Monster,id};
    if((!unit.alive() && (!unit.owner || unit.conversion)) || !unit.nativeAi()) {
        ports_.frameEvents.cancelHandler(owner,FrameEventType::AiThink,0);return;
    }
    due=std::max(due,ports_.frameEvents.currentFrame()+1);
    auto timer=ports_.frameEvents.prepareInsertion({{},owner,FrameEventType::AiThink,due});
    ports_.frameEvents.cancelHandler(owner,FrameEventType::AiThink,0);
    ports_.frameEvents.publish(std::move(timer));
}
DomainResult<> System::schedulePresetReanimation(EntityId id,uint64_t tick) {
    const auto *unit=find(id);if(!unit || !unit->rule.selfResurrection) return {DomainStatus::NotImplemented,{}};
    if(unit->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto candidate=*unit;
    const auto due=tick+250+limitedRandom(candidate.combatRandom,50);
    ++candidate.revision;
    auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,due,false,0,34});
    auto publication=prepareActorPublication({{id,std::move(candidate)}},0);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishActors(publication);ports_.frameEvents.publish(std::move(timer));
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::modifierEvent(const FrameEvent &event,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    if(event.customId==41) {
        const auto *unit=find(event.owner.unit);
        if(!unit || !unit->alive()) return {DomainStatus::Applied,std::monostate{}};
        if(unit->revision==UINT64_MAX || unit->aiThinkRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto candidate=*unit;
        const bool neutral=!candidate.moving && candidate.route.empty() && candidate.busyUntil<=tick &&
            candidate.riseUntil<=tick && candidate.reactionUntil<=tick && candidate.aiPose==1;
        if(neutral || candidate.rule.wakeAiOutsideNeutral) {
            candidate.aiThinkAt=tick+2;++candidate.aiThinkRevision;
            candidate.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,unit->id},FrameEventType::AiThink,tick+2}));
        }
        candidate.initialWakeTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,unit->id},FrameEventType::MonsterModifier,tick+75,false,0,41}));
        ++candidate.revision;
        auto publication=prepareActorPublication({{unit->id,std::move(candidate)}},0);
        if(!validateActors(publication)) return {DomainStatus::Stale,{}};
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,unit->id},FrameEventType::AiThink,0);
        publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
    }
    if(event.customId==34) {
        const auto *unit=find(event.owner.unit);
        if(!unit || unit->alive()) return {DomainStatus::Applied,std::monostate{}};
        if(!unit->rule.selfResurrection || !collision) return {DomainStatus::NotImplemented,{}};
        if(!ports_.areas.loaded(unit->area,unit->position)) {
            auto next=event;next.handle={};next.due=tick+1;
            auto timer=ports_.frameEvents.prepareInsertion(next);ports_.frameEvents.publish(std::move(timer));
            return {DomainStatus::Applied,std::monostate{}};
        }
        if(unit->identity.rank==MonsterRank::Unique || unit->identity.rank==MonsterRank::SuperUnique || unit->identity.rank==MonsterRank::Champion ||
            unit->alignment()==UnitAlignment::Good) return {DomainStatus::Applied,std::monostate{}};
        for(const auto state:ports_.effects.unitStates(unit->id,tick))
            if(unit->rule.states->at(state).corpseUnselectable) return {DomainStatus::Applied,std::monostate{}};
        const auto &area=ports_.areas.at(unit->area).definition;
        const auto global=unit->position+area.origin;
        const auto room=ports_.areas.roomAtPosition(unit->area,unit->position,int(std::floor(global.x)),int(std::floor(global.y)));
        if(!room) return {DomainStatus::Unavailable,{}};
        if(ports_.areas.nativeCollisionMask(*room,int(std::floor(global.x)),int(std::floor(global.y)),1,0x3c01,collision)) {
            auto next=event;next.handle={};next.due=tick+uint64_t(unit->rule.selfResurrection->delay);
            auto timer=ports_.frameEvents.prepareInsertion(next);ports_.frameEvents.publish(std::move(timer));
            return {DomainStatus::Applied,std::monostate{}};
        }
        if(unit->selfResurrections>=2 || !consumableCorpse(unit->id,unit->area,tick) || unit->alignment()!=UnitAlignment::Evil)
            return {DomainStatus::Applied,std::monostate{}};
        if(unit->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto candidate=*unit;attributes::MonsterResourceTrace resources(candidate,tick);
        resources.sampleSources(ports_.effects.unitStatSources(unit->id,tick));resources.seedLife(resources.read().maximum);
        auto life=resources.take();candidate.nativeStats=std::move(life.intrinsic);
        candidate.life=life.life.raw();candidate.maximumLife=life.maximum.raw();candidate.modeAlive=false;
        // UMod34 clears the NOXP | NOTC flags installed by ResurrectUnit.
        candidate.rule.noTreasure=candidate.rule.noExperience=false;candidate.rewardComplete=false;
        candidate.targetFlagsDisabled=true;
        candidate.riseUntil=candidate.busyUntil=tick+uint64_t(candidate.rule.selfResurrection->ticks);candidate.riseMode=8;
        candidate.neutralAt=candidate.riseUntil;candidate.corpse={};candidate.killer={};
        candidate.ownershipDeathHandled=false;++candidate.selfResurrections;++candidate.revision;
        candidate.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,unit->id},FrameEventType::AiThink,tick+51}));
        auto publication=prepareActorPublication({{unit->id,std::move(candidate)}},0);
        if(!validateActors(publication)) return {DomainStatus::Stale,{}};
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},unit->area},
            {HitFact{unit->id,1,unit->area,128,false,unit->position,0,8}}});
        if(!sent) return {sent.status,{}};
        publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
    }
    if(event.customId!=21) return {DomainStatus::NotImplemented,{}};
    const auto *unit=find(event.owner.unit);if(!unit || !unit->alive()) return {DomainStatus::Applied,std::monostate{}};
    // MONUMOD_TEMPSUMMON retries three frames later while the mode cannot
    // be interrupted. Do not reduce HP first and invent a delayed death.
    if(ports_.effects.unitStates(unit->id,tick).contains(unit->rule.uninterruptableState)) {
        auto next=event;next.handle={};next.due=tick+3;
        auto timer=ports_.frameEvents.prepareInsertion(next);ports_.frameEvents.publish(std::move(timer));
        return {DomainStatus::Applied,std::monostate{}};
    }
    return scriptDeath(unit->id,tick);
}
bool System::frozen(EntityId id,uint64_t tick) const {
    const auto *actor=find(id);const auto *states=ports_.effects.unitEffects(id);
    return actor && actor->alive() && states && states->hasState(actor->rule.frozenState.id,tick);
}
DomainResult<> System::resetAiSelector(EntityId id,uint64_t identity,uint64_t tick) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end()) {ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});return {DomainStatus::InvalidActor,{}};}
    auto &actor=found->second;
    const auto current=std::find_if(actor.aiResetTimers.begin(),actor.aiResetTimers.end(),[&](const auto &timer){return timer.identity==identity;});
    if(current==actor.aiResetTimers.end() || current->due>tick) return {DomainStatus::Applied,std::monostate{}};
    const bool frozen=this->frozen(id,tick);
    if(frozen && current->continuous) return {DomainStatus::Applied,std::monostate{}};
    const bool allegianceChanged=!frozen && (actor.curseControl.ai==CurseAi::Confuse || bool(actor.attractionTarget));
    if(actor.revision==UINT64_MAX || (allegianceChanged && actor.allegianceRevision==UINT64_MAX)) return {DomainStatus::Capacity,{}};
    auto next=actor.aiResetTimers;
    if(!current->continuous) std::erase_if(next,[&](const auto &timer){return timer.identity==identity;});
    std::vector<FrameEvent> events;events.reserve(next.size());
    for(auto it=next.rbegin();it!=next.rend();++it)
        events.push_back({{it->identity},{},FrameEventType::AiReset,it->due,it->continuous});
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::AiReset,events);
    // MonsterMode's selector reset leaves the selected DimVision/Terror/Taunt
    // AI function untouched. Several timers remain distinct native callbacks.
    // MONSTERMODE_EventHandler skips AIRESET for a living frozen unit. The
    // timed queue still consumes that callback; it is not retried after thaw.
    if(!frozen) {
        if(actor.curseControl.ai==CurseAi::Confuse) actor.curseControl={};
        actor.attractionTarget={};actor.attractionUntil=0;
    }
    actor.aiResetTimers=std::move(next);
    ++actor.revision;if(allegianceChanged) ++actor.allegianceRevision;
    ports_.frameEvents.publish(std::move(timers));
    return {DomainStatus::Applied,std::monostate{}};
}
}
