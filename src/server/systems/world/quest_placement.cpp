#include "system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include "server/systems/spatial/system.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::world {
std::optional<Vec> System::nativeActSpawnLocation(RegionId region,int tile) {
    const auto found=ports_.areas.areas_.find(region);if(found==ports_.areas.areas_.end()) return {};
    auto &state=found->second;const auto &area=state.definition;
    if(tile<0 || tile>13) throw std::invalid_argument("Invalid native spawn tile index");
    if(!state.levelRandom) state.levelRandom=area.initialLevelRandom;
    const auto &rooms=area.activation.rooms();
    std::vector<size_t> order;
    for(size_t index=0;index<rooms.size();++index) order.push_back(index);
    std::sort(order.begin(),order.end(),[&](size_t first,size_t second) {
        return area.populationRooms.at(first).allocationOrder>area.populationRooms.at(second).allocationOrder;
    });
    const RoomBounds *selected=nullptr;
    std::optional<Vec> point;
    if(area.fixedPosition && tile!=13) {
        const auto matches=[&](const NativeTileInfo &entry) {
            return entry.index==tile || (tile==0 && entry.index>=0 && entry.index<=4) || (tile==5 && entry.index>=5 && entry.index<=9);
        };
        const auto count=std::count_if(area.tileInfo.begin(),area.tileInfo.end(),matches);
        int remaining=count?1+int(limitedRandom(*state.levelRandom,uint32_t(count))):0;
        for(const auto &entry:area.tileInfo) {
            if(matches(entry)) --remaining;
            if(remaining<=0) {point=Vec{float(entry.x*5),float(entry.y*5)}-area.origin;break;}
        }
        if(!point) return {};
        selected=area.activation.room(*point);
    } else {
        for(const auto index:order) {
            const auto &layout=area.populationRooms.at(index);
            if(!layout.waypoint) continue;
            selected=&rooms[index];
            if(layout.waypointTile) point=*layout.waypointTile*5.f-area.origin;
            break;
        }
        if(!selected && area.fixedPosition) return {};
        if(!selected) for(const auto index:order) {
            if(area.populationRooms.at(index).warp) {selected=&rooms[index];break;}
        }
        if(!selected) {
            const Vec center{float((area.tileX+area.tileWidth/2-2)*5)-area.origin.x,
                float((area.tileY+area.tileHeight/2-2)*5)-area.origin.y};
            selected=area.activation.room(center);
        }
        if(!selected && !order.empty()) selected=&rooms[order[limitedRandom(*state.levelRandom,uint32_t(order.size()))]];
        if(selected && !point) point=Vec{float(((int(area.origin.x)+selected->x)/5+selected->width/10)*5)-area.origin.x,
            float(((int(area.origin.y)+selected->y)/5+selected->height/10)*5)-area.origin.y};
    }
    if(!selected || !point) return {};
    activate(region,{float(selected->x)+.5f,float(selected->y)+.5f});
    return point;
}
std::optional<QuestPosition> System::nativeFreeSpace(RegionId region,Vec observer,Vec requested,int size) const {
    const auto *area=ports_.areas.find(region);if(!area) return {};
    const auto global=requested+area->definition.origin;
    const auto anchor=observer+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(region,observer,int32_t(std::floor(anchor.x)),int32_t(std::floor(anchor.y)));
    if(!room) return {};
    const auto found=ports_.areas.nativeFreeSpace(*room,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)),size,ports_.spatial.nativeCollisionSample());
    return found?std::optional<QuestPosition>{{found->room.area,found->position}}:std::nullopt;
}
std::optional<QuestPosition> System::nativeActSpawnLocationEx(RegionId region,int tile,int size,const AreaStore::CollisionSample &sample) {
    if(size<0 || size>3) return {};
    const auto point=nativeActSpawnLocation(region,tile);
    if(!point) return {};
    const auto &area=ports_.areas.at(region).definition;
    const auto global=*point+area.origin;
    const auto room=ports_.areas.roomAtPosition(region,*point,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return {};
    const auto free=ports_.areas.nativeFreeCoordinates(*room,nativeAdd(int32_t(std::floor(global.x)),3),
        nativeAdd(int32_t(std::floor(global.y)),3),size,0x1c09,sample?sample:ports_.spatial.nativeCollisionSample());
    return free?std::optional<QuestPosition>{{free->room.area,free->position,free->room}}:std::nullopt;
}
std::optional<QuestPosition> System::nativeDurielPortalPosition(RegionId region) {
    const auto point=nativeActSpawnLocationEx(region,0,2);
    if(!point) return {};
    const auto &area=ports_.areas.at(point->area).definition;
    const auto global=point->position+area.origin;
    const auto room=ports_.areas.roomAtPosition(point->area,point->position,int(global.x),int(global.y));
    if(!room) return {};
    const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),2,0x1c09,ports_.spatial.nativeCollisionSample());
    return free?std::optional<QuestPosition>{{free->room.area,free->position}}:std::nullopt;
}
std::optional<QuestPosition> System::nativePlayerPosition(RegionId region,Vec position,EntityId self) const {
    auto exclusion=ports_.areas.excludeCollision(self);
    const auto &area=ports_.areas.at(region).definition;
    const auto global=position+area.origin;
    const int x=int(std::floor(global.x+.5f)),y=int(std::floor(global.y+.5f));
    const auto room=ports_.areas.roomAtPosition(region,position+Vec{.5f,.5f},x,y);
    if(!room) return {};
    const auto free=ports_.areas.nativeFreeCoordinates(*room,x,y,2,playerMovement.mask,
        ports_.spatial.nativeCollisionSample(playerMovement.mask));
    return free?std::optional<QuestPosition>{{free->room.area,free->position,free->room}}:std::nullopt;
}
std::optional<QuestPosition> System::nativeWaypointPosition(RegionId region,bool changeAct) {
    const auto &area=ports_.areas.at(region).definition;
    const int tile=area.town || int(region)==46 || int(region)==74?13:0;
    auto point=nativeActSpawnLocationEx(region,tile,2);
    if(!point) return {};
    if(changeAct) {
        const auto &destination=ports_.areas.at(point->area).definition;
        const auto global=point->position+destination.origin;
        const auto room=ports_.areas.roomAtPosition(point->area,point->position,int(global.x),int(global.y));
        if(!room) return {};
        const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),2,0x1c89,
            ports_.spatial.nativeCollisionSample(),{},0,false,50,5);
        if(!free) return {};
        point=QuestPosition{free->room.area,free->position};
    }
    nativeActSpawnLocation(region,tile);
    return point;
}
std::optional<QuestPosition> System::nativeLinkPortalPosition(RegionId region,bool durielOpening) {
    const auto start=nativeActSpawnLocationEx(region,11,3);
    if(!start) return {};
    const auto &initial=ports_.areas.at(start->area).definition;
    auto global=start->position+initial.origin;
    auto room=start->room?start->room:ports_.areas.roomAtPosition(start->area,start->position,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return {};
    const auto originalRoom=*room;
    if(durielOpening) {
        const auto tomb=nativeActSpawnLocationEx(RegionId{40},12,3);
        if(tomb && tomb->room) {
            const auto checked=ports_.areas.nativeFreeCoordinates(*tomb->room,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)),3,0xbe11,
                ports_.spatial.nativeCollisionSample(),{},0,false,50,7);
            if(checked) {
                const auto replacement=tomb->position+ports_.areas.at(tomb->area).definition.origin;
                const auto &origin=ports_.areas.at(checked->room.area).definition.origin;
                room=ports_.areas.roomAtPosition(checked->room.area,Vec{float(checked->room.x*5),float(checked->room.y*5)}-origin,
                    int32_t(std::floor(replacement.x)),int32_t(std::floor(replacement.y)));
                if(room) global=replacement;
                else {room=originalRoom;global=start->position+initial.origin;}
            }
        }
    }
    const auto found=ports_.areas.nativeFreeCoordinates(*room,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)),3,0xbe11,
        ports_.spatial.nativeCollisionSample(),{},0,false,50,5);
    if(found) return QuestPosition{found->room.area,found->position,found->room};
    const auto fallback=nativeActSpawnLocationEx(region,11,3);
    const auto destination=fallback && fallback->room?*fallback->room:originalRoom;
    return QuestPosition{destination.area,global-ports_.areas.at(destination.area).definition.origin,destination};
}
// QUESTS_GetFreePosition: D2MOO 5596f5c (MIT, docs/licenses/D2MOO.txt),
// with the two differing branches checked against retail 1.13c RVA 34130.
void System::rebindObject(RegionId region,EntityId previous,EntityId current) {
    auto &area=ports_.areas.areas_.at(region).definition;
    for(auto &object:area.objects) if(object.id==previous) {object.id=current;break;}
}
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
