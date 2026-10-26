#include "../system.hpp"
#include "server/area_store.hpp"

namespace d2x::server::monsters {
DomainResult<> System::startPd2DashMotion(EntityId id,Vec target) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    // Current St76/Do185 use retail PATHTYPE_LEAP (9), with both the
    // path collision mask and the unit's occupancy cleared during transit.
    unit.pd2DashAbsent=true;refreshCollision(unit);
    unit.skillMotion=Actor::SkillMotion{target,UINT64_MAX,unit.interruption,
        float(uint32_t(unit.rule.nativeVelocity)<<9)*25.f/4096.f,false,{target}};
    unit.skillMotion->eventDriven=true;unit.skillMotion->collision=MovementCollisionRule{0,unit.rule.size};
    unit.pathStep.reset();unit.movementTarget={};unit.movementGoal=target;unit.pathType=9;
    ++unit.revision;return {DomainStatus::Applied,std::monostate{}};
}
}
