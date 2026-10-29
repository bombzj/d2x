#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include <memory>
#include <cmath>
#include <algorithm>

namespace d2x::server::monsters {
DomainResult<> System::retireQuestNpc(EntityId id) {
    if(restoring_ && restoring_->actor==id) return {DomainStatus::Capacity,{}};
    if(const auto *unit=find(id)) {
        if(!unit->questNpc || unit->hostile || unit->owner) return {DomainStatus::InvalidActor,{}};
        const auto removed=unit->alive()?moveNpc(id,unit->area,unit->position,unit->position,true):remove(id);
        if(!removed) return removed;
    }
    for(auto &[room,units]:state_.inactive) {
        (void)room;
        std::erase_if(units,[&](const auto &unit){return unit.id==id && unit.questNpc && !unit.hostile;});
    }
    std::erase_if(state_.inactive,[](const auto &entry){return entry.second.empty();});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::departQuestNpc(EntityId id,uint64_t tick) {
    const auto *unit=find(id);
    if(!unit || !unit->questNpc || unit->hostile || unit->owner) return {DomainStatus::InvalidActor,{}};
    if(!unit->alive()) return {DomainStatus::Applied,std::monostate{}};
    auto dead=prepareDeathActor(*unit,tick);if(!dead) return {dead.status,{}};
    auto &candidate=*dead.value;
    // Fn031 enters DD directly: no DT animation, kill award, UMod death
    // damage or killed-room GUID. The corpse leaves through normal room life.
    recordNativeMode(candidate,12);candidate.busyUntil=tick;candidate.nativeActionUntil=0;candidate.neutralAt.reset();
    candidate.movementGoal=candidate.position;candidate.pathStep.reset();candidate.rewardComplete=true;
    candidate.killer={};candidate.wanderTargetOrder=candidate.specialTargetOrder=0;
    auto publication=ports_.effects.prepareDamageStates(std::move(candidate),{},tick);
    if(!ports_.effects.validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},unit->area},std::move(publication.facts)});
        if(!sent) return {sent.status,{}};
    }
    ports_.effects.publishAuraUnits(publication);
    ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::setNpcHomeCommand(EntityId id,Vec global) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.questNpc || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    unit.aiHomeCommand=Vec{std::floor(global.x),std::floor(global.y)};++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::rebindNpcMapAi(EntityId id,const std::vector<MonsterRule::MapAiNode> &nodes) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.questNpc || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    if(found->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto candidate=found->second;candidate.rule.mapAi=nodes;
    // Inactive reconstruction must retain the transferred path, rather than
    // the original class prototype or an already consumed quest buffer.
    auto admission=std::make_shared<MonsterRule>(*candidate.admissionRule);
    admission->mapAi=nodes;candidate.admissionRule=std::move(admission);++candidate.revision;
    auto publication=prepareActorPublication({{id,std::move(candidate)}},0);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
}
}
