#include "system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/systems/spatial/system.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::world {
std::optional<QuestPosition> System::nativeFreeSpace(RegionId region,Vec observer,Vec requested,int size) const {
    const auto *area=ports_.areas.find(region);if(!area) return {};
    const auto global=requested+area->definition.origin;
    const auto anchor=observer+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(region,observer,int32_t(std::floor(anchor.x)),int32_t(std::floor(anchor.y)));
    if(!room) return {};
    const auto found=ports_.areas.nativeFreeSpace(*room,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)),size,ports_.spatial.nativeCollisionSample());
    return found?std::optional<QuestPosition>{{found->room.area,found->position}}:std::nullopt;
}
std::optional<QuestPosition> System::nativeLinkPortalPosition(RegionId region,Vec requested) const {
    const auto *area=ports_.areas.find(region);if(!area) return {};
    const auto global=requested+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(region,requested,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return {};
    const auto found=ports_.areas.nativeFreeCoordinates(*room,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)),3,0xbe11,
        ports_.spatial.nativeCollisionSample(),{},0,false,50,5);
    // CreateLinkPortal repeats FindActSpawnLocationEx after search failure,
    // but retains the original coords for the actual object constructor.
    return found?std::optional<QuestPosition>{{found->room.area,found->position}}:std::optional<QuestPosition>{{region,requested}};
}
// QUESTS_GetFreePosition: D2MOO 5596f5c (MIT, docs/licenses/D2MOO.txt),
// with the two differing branches checked against retail 1.13c RVA 34130.
void System::retainControllerRoom(RegionId region,Vec position,bool retain) {
    const auto found=ports_.areas.areas_.find(region);if(found==ports_.areas.areas_.end()) return;
    auto &area=found->second;const auto &layout=area.definition.activation;
    const auto *room=layout.room(position);if(!room) return;
    const auto index=size_t(room-layout.rooms().data());
    if(index<area.rooms.size()) area.rooms[index].portalRetained=retain;
}
std::optional<QuestPosition> System::questFreePosition(RegionId region,Vec roomPosition,Vec requested,int size,int radius,uint16_t mask) const {
    if(size<0 || radius<=1) return {};
    const auto &initial=ports_.areas.at(region).definition;
    const auto *initialBounds=initial.activation.room(roomPosition);if(!initialBounds) return {};
    const RoomView initialRoom{region,(int(initial.origin.x)+initialBounds->x)/5,(int(initial.origin.y)+initialBounds->y)/5};
    const auto bounds=[&](RoomView view) {
        const auto &area=ports_.areas.at(view.area).definition;
        const auto &rooms=area.activation.rooms();
        const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
            return (int(area.origin.x)+r.x)/5==view.x && (int(area.origin.y)+r.y)/5==view.y;
        });
        if(room==rooms.end()) throw std::logic_error("Missing native quest placement room");
        return RoomBounds{int(area.origin.x)+room->x,int(area.origin.y)+room->y,room->width,room->height};
    };
    const auto find=[&](RoomView from,int x,int y) {
        const auto &area=ports_.areas.at(from.area).definition;
        return ports_.areas.roomAtPosition(from.area,Vec{float(from.x*5),float(from.y*5)}-area.origin,x,y);
    };
    const auto sample=mask==0x400?std::function<uint16_t(RoomView,int32_t,int32_t)>{}:ports_.spatial.nativeCollisionSample(mask);
    const auto clear=[&](RoomView room,int x,int y) {
        for(int dy=-size;dy<=size;++dy) for(int dx=-size;dx<=size;++dx) {
            const auto cell=find(room,nativeAdd(x,dx),nativeAdd(y,dy));if(!cell) return false;
            const auto &area=ports_.areas.at(cell->area).definition;
            const auto collision=sample?sample(*cell,nativeAdd(x,dx),nativeAdd(y,dy)):
                area.collision.movementMask(nativeSubtract(nativeAdd(x,dx),int(area.origin.x)),
                    nativeSubtract(nativeAdd(y,dy),int(area.origin.y)));
            if(collision&mask) return false;
        }
        return true;
    };
    const int half=size>>1;
    const int centerX=nativeSubtract(int32_t(std::floor(requested.x+initial.origin.x)),half);
    const int centerY=nativeSubtract(int32_t(std::floor(requested.y+initial.origin.y)),half);
    int currentX=centerX;auto lastBounds=bounds(initialRoom);
    // Retail 1.13c RVA 34130 uses the supplied radius (D2MOO fixes it at 100).
    // It also really compares nY with the room's upper X bound at RVA 3420f.
    // Preserve both the carried nX and carried room bounds between rows.
    for(int low=-1,high=1;high<radius;--low,++high) for(int y=high;y>low;y-=2) {
        const int currentY=nativeAdd(centerY,y);
        std::optional<RoomView> row=initialRoom;
        if(currentY<lastBounds.y || currentY>=nativeAdd(lastBounds.y,lastBounds.height)) row=find(initialRoom,currentX,currentY);
        if(!row) continue;
        for(int x=low;x<high;x+=2) {
            lastBounds=bounds(*row);currentX=nativeAdd(centerX,x);
            std::optional<RoomView> room=*row;
            if(currentX<lastBounds.x || currentY>=nativeAdd(lastBounds.x,lastBounds.width)) room=find(*row,currentX,currentY);
            if(!room || !clear(*room,currentX,currentY) || currentX<lastBounds.x || currentX>=nativeAdd(lastBounds.x,lastBounds.width) ||
                currentY<lastBounds.y || currentY>=nativeAdd(lastBounds.y,lastBounds.height)) continue;
            const int finalX=nativeAdd(currentX,half),finalY=nativeAdd(currentY,half);
            const auto destination=find(initialRoom,finalX,finalY);if(!destination) return {};
            return QuestPosition{destination->area,Vec{float(finalX),float(finalY)}-ports_.areas.at(destination->area).definition.origin};
        }
    }
    return {};
}
}
