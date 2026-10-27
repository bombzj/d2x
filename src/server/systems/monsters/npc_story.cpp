#include "system.hpp"
#include <memory>
#include <cmath>
#include <algorithm>

namespace d2x::server::monsters {
DomainResult<> System::retireQuestNpc(EntityId id) {
    if(restoring_ && restoring_->actor==id) return {DomainStatus::Capacity,{}};
    if(const auto *unit=find(id)) {
        if(!unit->questNpc || unit->hostile || unit->owner) return {DomainStatus::InvalidActor,{}};
        const auto removed=moveNpc(id,unit->area,unit->position,unit->position,true);
        if(!removed) return removed;
    }
    for(auto &[room,units]:state_.inactive) {
        (void)room;
        std::erase_if(units,[&](const auto &unit){return unit.id==id && unit.questNpc && !unit.hostile;});
    }
    std::erase_if(state_.inactive,[](const auto &entry){return entry.second.empty();});
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
