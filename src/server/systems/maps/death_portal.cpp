#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"

namespace d2x::server::maps {
DomainResult<PointTarget> System::preparePortalLanding(const pd2::MapPortalProgram &program,
    std::optional<PointTarget> &anchor,std::optional<PointTarget> &landing) {
    const auto region=RegionId(program.destination);
    const auto requested=ports_.world.requestMapArea(region,state_.instance);
    if(!requested) return {requested.status,{}};
    const auto *target=ports_.areas.find(region);
    if(!target) return {DomainStatus::PendingContent,{}};
    if(!anchor) {
        const auto point=program.levelOffset?std::optional{Vec{float(target->definition.tileX*5),float(target->definition.tileY*5)}+
            *program.levelOffset-target->definition.origin}:ports_.world.nativeActSpawnLocation(region,program.tile);
        if(!point || !target->definition.activation.room(*point)) return {DomainStatus::Unavailable,{}};
        anchor=PointTarget{region,target->generation,*point};
    }
    // Spawn tile selection consumes the level seed once. Room preparation is
    // asynchronous; absence of its collision grid is a wait, not no landing.
    ports_.world.activate(anchor->area,anchor->position);
    if(!ports_.areas.loaded(anchor->area,anchor->position)) return {DomainStatus::PendingContent,{}};
    if(!landing) {
        const auto point=program.levelOffset?std::optional{world::QuestPosition{anchor->area,anchor->position}}:
            ports_.world.nativeActSpawnLocationExAt(anchor->area,anchor->position,3);
        if(!point) return {DomainStatus::Unavailable,{}};
        const auto at=point->position+program.offset;
        const auto &area=ports_.areas.at(point->area);
        if(!area.definition.activation.room(at)) return {DomainStatus::Unavailable,{}};
        landing=PointTarget{point->area,area.generation,at};
    }
    ports_.world.activate(landing->area,landing->position);
    if(!ports_.areas.loaded(landing->area,landing->position)) return {DomainStatus::PendingContent,{}};
    if(program.destinationFreeSpace) {
        const auto free=ports_.world.nativeFreeSpace(landing->area,landing->position,landing->position,1);
        if(!free) return {DomainStatus::Unavailable,{}};
        *landing=PointTarget{free->area,ports_.areas.at(free->area).generation,free->position};
    }
    return {DomainStatus::Applied,*landing};
}
DomainResult<> System::advanceDeathPortal(RegionId sourceArea,Death &death,
    const pd2::MapPortalProgram &program,int sourceDefinition,Vec sourcePosition,int initialMode,TickContext tick) {
    if(death.portalPhase==0) {
        const auto created=ports_.objects.admit({sourceDefinition,sourceArea,sourcePosition,{},{},initialMode,tick.tick});
        if(isDeferred(created.status)) return {created.status,{}};
        if(created) {
            death.portalObject=*created.value;
            ports_.world.retainControllerUnitRoom(death.portalObject,sourceArea,sourcePosition,true);
        }
        // The original caller still attempts the destination if allocation of
        // the source failed. A permanent failure is never a retry instruction.
        death.portalPhase=1;
    }
    if(death.portalPhase==1) {
        const auto point=preparePortalLanding(program,death.portalAnchor,death.portalLanding);
        if(isDeferred(point.status)) return {point.status,{}};
        death.portalPhase=point?2:6;
    }
    if(death.portalPhase==2) {
        const auto &point=*death.portalLanding;
        ports_.world.activate(point.area,point.position);
        if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
        const auto created=ports_.objects.admit({program.definition,point.area,point.position,{},{},0,tick.tick});
        if(isDeferred(created.status)) return {created.status,{}};
        if(created) {
            death.portalDestination=*created.value;
            ports_.world.retainControllerUnitRoom(death.portalDestination,point.area,point.position,true);
        }
        death.portalPhase=3;
    }
    if(death.portalPhase==3) {
        if(death.portalObject && death.portalDestination) {
            const auto linked=ports_.objects.linkNativePortals(death.portalObject,death.portalDestination,program.mutual,tick.tick);
            if(isDeferred(linked.status)) return linked;
        }
        death.portalPhase=4;
    }
    if(death.portalPhase==4) {
        if(death.portalObject) {
            const auto changed=ports_.objects.setNativeMode(death.portalObject,program.sourceMode,tick.tick,program.sourceMode==1);
            if(isDeferred(changed.status)) return changed;
        }
        death.portalPhase=5;
    }
    if(death.portalPhase==5) {
        if(death.portalDestination) {
            const auto changed=ports_.objects.setNativeMode(death.portalDestination,program.destinationMode,tick.tick,program.destinationMode==1);
            if(isDeferred(changed.status)) return changed;
        }
        death.portalPhase=6;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
