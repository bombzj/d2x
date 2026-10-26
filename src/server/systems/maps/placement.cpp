#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/maps/pd2/placement.hpp"
namespace d2x::server::maps {
void System::prepareDropPosition(const monsters::Actor &unit,Death &death,bool requireSearch) const {
    const auto &area=ports_.areas.at(unit.area).definition;
    const auto global=unit.position+area.origin;
    auto room=ports_.areas.roomAtPosition(unit.area,unit.position,int(global.x),int(global.y));
    death.rewardPositionPrepared=true;death.rewardContext=room.has_value();if(!room) return;
    const auto sample=ports_.spatial.nativeCollisionSample();
    const auto locate=[&](RoomView source,int x,int y) {
        const auto &owner=ports_.areas.at(source.area).definition;
        return ports_.areas.roomAtPosition(source.area,Vec{float(source.x*5),float(source.y*5)}-owner.origin,x,y);
    };
    const auto point=pd2::scenePlacement(int(global.x),int(global.y),7,[&](int x,int y) {
        if(!room) return false;
        room=locate(*room,x,y);if(!room) return false;
        // 387BB0 calls CheckMaskWithSizeXY(2,2), a 2x2 rectangle, rather
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
