#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"

namespace d2x::server::maps {
DomainResult<> System::initializeSceneTown(TickContext tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!state_.instance || ports_.travel.read().pd2SceneOpening || ports_.travel.read().pd2SceneEntrants.empty()) return done();
    const int program=state_.instance->controller;
    if(program<41 || program>45 || program==44 || (program!=41 && state_.instance->bossDifficulty!=0)) return done();
    const auto *town=ports_.areas.find(RegionId(109));if(!town) return {DomainStatus::PendingContent,{}};
    constexpr std::array<Vec,3> points{{{0x13d4,0x13ff},{0x13cc,0x13f4},{0x13d7,0x13f6}}};
    if(state_.sceneTownStage==0) {
        const auto point=points[0]-town->definition.origin;
        pd2::MapPortalProgram portal{state_.instance->destination.level,0,587,1,1,{},false};portal.destinationFreeSpace=true;
        const auto created=advanceDeathPortal(RegionId(109),state_.sceneTownPortal,portal,595,point,0,tick);
        if(!created) return created;
        if(state_.sceneTownPortal.portalObject) bind(7,state_.sceneTownPortal.portalObject,RegionId(109),point,true);
        ++state_.sceneTownStage;
    }
    while(state_.sceneTownStage<3) {
        const size_t index=state_.sceneTownStage;const auto point=points[index]-town->definition.origin;
        auto id=state_.bindings[7+index].id;
        if(!id) {
            ports_.world.activate(RegionId(109),point);
            if(!ports_.areas.loaded(RegionId(109),point)) return {DomainStatus::PendingContent,{}};
            const auto created=ports_.objects.admit({595,RegionId(109),point,{},{},0,tick.tick});
            if(isDeferred(created.status)) return {created.status,{}};
            if(created) {id=*created.value;bind(int(7+index),id,RegionId(109),point,true);}
        }
        if(id && state_.sceneTownPortal.portalDestination) {
            const auto linked=ports_.objects.linkNativePortals(id,state_.sceneTownPortal.portalDestination,false,tick.tick);
            if(isDeferred(linked.status)) return linked;
        }
        ++state_.sceneTownStage;
    }
    if(program==41 && state_.sceneTownStage==3) {
        const RegionId region{185};const auto *area=ports_.areas.find(region);
        if(!area) return {DomainStatus::PendingContent,{}};
        auto &cursor=state_.sceneTownReturn;
        if(!cursor.evaluated) {
            const auto point=ports_.world.nativeActSpawnLocationEx(region,0,3);
            if(point) if(const auto free=ports_.world.nativeFreeSpace(region,point->position,point->position+Vec{-3,-3},1)) cursor.portalPosition=free->position;
            cursor.evaluated=true;
        }
        if(cursor.portalPosition) {
            pd2::MapPortalProgram portal{109,0,587,1,1,{},false};
            portal.levelOffset=Vec{0x13d2,0x13fa}-Vec{float(town->definition.tileX*5),float(town->definition.tileY*5)};
            const auto created=advanceDeathPortal(region,cursor,portal,60,*cursor.portalPosition,0,tick);if(!created) return created;
        }
        ++state_.sceneTownStage;
    }
    return done();
}
}
