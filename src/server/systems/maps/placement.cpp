#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/maps/pd2/placement.hpp"
namespace d2x::server::maps {
std::optional<PointTarget> System::bossSpawnPosition(RegionId area,Vec point,bool search) const {
    if(search) return prepareScenePosition(area,point,12);
    const auto free=ports_.world.nativeFreeSpace(area,point,point,1);
    return free?std::optional{PointTarget{free->area,ports_.areas.at(free->area).generation,free->position}}:std::nullopt;
}
DomainResult<EntityId> System::bossSpawn(const PointTarget &point,int definition,std::string key,TickContext tick) {
    const auto *area=ports_.areas.find(point.area);
    if(!area || area->generation!=point.generation) return {DomainStatus::Stale,{}};
    ports_.world.activate(point.area,point.position);
    if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
    return ports_.monsters.admitPd2SceneMonster(point.area,point.position,definition,std::move(key),tick.tick);
}
std::optional<PointTarget> System::prepareScenePosition(RegionId region,Vec position,int size,int radius,bool offset,uint16_t mask) const {
    const auto &area=ports_.areas.at(region).definition;
    const auto global=position+area.origin;
    auto room=ports_.areas.roomAtPosition(region,position,int(global.x),int(global.y));
    if(!room || size<1) return {};
    const auto sample=ports_.spatial.nativeCollisionSample();
    const auto locate=[&](RoomView source,int x,int y) {
        const auto &owner=ports_.areas.at(source.area).definition;
        return ports_.areas.roomAtPosition(source.area,Vec{float(source.x*5),float(source.y*5)}-owner.origin,x,y);
    };
    const auto point=pd2::scenePlacement(int(global.x),int(global.y),radius,[&](int x,int y) {
        if(!room) return false;
        room=locate(*room,x,y);if(!room) return false;
        // Common CheckMaskWithSizeXY uses a centered rectangle, including
        // exactly size cells. 38A170 requests mask805 and a fixed BFS bound8.
        for(int row=y-size/2;row<y-size/2+size;++row)
            for(int column=x-size/2;column<x-size/2+size;++column) {
                const auto owner=locate(*room,column,row);
                if(!owner || (sample(*owner,column,row)&mask)) return false;
            }
        return true;
    });
    if(!point || !room) return {};
    const int x=point->first-(offset?2:0),y=point->second-(offset?2:0);
    room=locate(*room,x,y);if(!room) return {};
    const auto &owner=ports_.areas.at(room->area);
    return PointTarget{room->area,owner.generation,Vec{float(x),float(y)}-owner.definition.origin};
}
void System::prepareDropPosition(const monsters::Actor &unit,Death &death,bool requireSearch) const {
    prepareDropPosition(unit.area,unit.position,death,requireSearch);
}
void System::prepareDropPosition(RegionId region,Vec position,Death &death,bool requireSearch) const {
    const auto &area=ports_.areas.at(region).definition;
    const auto global=position+area.origin;
    auto room=ports_.areas.roomAtPosition(region,position,int(global.x),int(global.y));
    death.rewardPositionPrepared=true;death.rewardContext=room.has_value();if(!room) return;
    const auto sample=ports_.spatial.nativeCollisionSample();
    const auto locate=[&](RoomView source,int x,int y) {
        const auto &owner=ports_.areas.at(source.area).definition;
        return ports_.areas.roomAtPosition(source.area,Vec{float(source.x*5),float(source.y*5)}-owner.origin,x,y);
    };
    const auto point=pd2::scenePlacement(int(global.x),int(global.y),7,[&](int x,int y) {
        if(!room) return false;
        room=locate(*room,x,y);if(!room) return false;
        // Current 36ED30 -> 389C20 -> 389B70 checks sizeXY(2,2), rather
        // than CheckMaskWithSize's size-2 cross.
        for(int row=y-1;row<=y;++row) for(int column=x-1;column<=x;++column) {
            const auto owner=locate(*room,column,row);
            if(!owner || (sample(*owner,column,row)&0x805)) return false;
        }
        return true;
    });
    if(!room || (requireSearch && !point)) return;
    const auto requested=point.value_or(std::pair{int(global.x),int(global.y)});
    const auto free=ports_.areas.nativeFreeSpace(*room,requested.first,requested.second,1,sample);
    if(free) death.rewardPosition=PointTarget{free->room.area,ports_.areas.at(free->room.area).generation,free->position};
}
}
