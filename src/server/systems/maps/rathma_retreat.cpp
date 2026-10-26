#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"

namespace d2x::server::maps {
DomainResult<> System::rathmaRetreatPortal(EntityId id,TickContext tick) {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !state_.instance || state_.instance->controller!=43) return {DomainStatus::InvalidActor,{}};
    auto [found,created]=state_.sceneRetreats.try_emplace(id);auto &portal=found->second;
    if(created) {
        const bool first=state_.phase==1;
        if(!first && state_.phase!=3) {state_.sceneRetreats.erase(found);return {DomainStatus::Unavailable,{}};}
        state_.phase=first?2:4;portal.eventDestination=first?162:163;
        portal.portalPosition=Vec{float(first?0x476c:0x47d3),float(first?0x35bf:0x394b)}-ports_.areas.at(unit->area).definition.origin;
    }
    pd2::MapPortalProgram program{portal.eventDestination,0,587,1,1,{},false};program.destinationFreeSpace=true;
    return advanceDeathPortal(unit->area,portal,program,585,*portal.portalPosition,0,tick);
}
}
