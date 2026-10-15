#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
#include <utility>

namespace d2x::server::monsters {
DomainResult<> System::modifierEvent(const FrameEvent &event,uint64_t tick) {
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
