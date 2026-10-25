#include "area_store.hpp"
#include "world/room_terrain.hpp"
#include "world/native_spawn.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <exception>
#include <cmath>

namespace d2x::server {
void AreaStore::recordMonsterDeath(EntityId id) noexcept {
    const auto unit=roomUnits_.find(id);
    if(unit==roomUnits_.end() || !unit->second.room) return;
    const auto [region,index]=*unit->second.room;
    auto &room=areas_.at(region).rooms.at(index);
    room.lastDead[room.deathIndex]=id;room.deathIndex=uint8_t((room.deathIndex+1)&3);
}
std::array<EntityId,4> AreaStore::roomLastDead(RoomView view) const noexcept {
    const auto *area=find(view.area);if(!area) return {};
    const auto *room=area->definition.activation.room(Vec{float(view.x*5),float(view.y*5)}-area->definition.origin);
    if(!room) return {};
    const auto index=size_t(room-area->definition.activation.rooms().data());
    return index<area->rooms.size() && area->rooms[index].loaded?area->rooms[index].lastDead:std::array<EntityId,4>{};
}
void AreaStore::validate(const AreaDefinition &initial) {
    const auto &grid = initial.collision;
    if (grid.width <= 0 || grid.height <= 0)
        throw std::runtime_error("Empty prepared authority area");
    const auto count = size_t(grid.width) * size_t(grid.height);
    if (!initial.roomTerrain || initial.roomTerrain->act()!=initial.act)
        throw std::runtime_error("Missing decoded native room inputs");
    if (grid.blocked.size() != count || grid.lightBlocked.size() != count ||
        grid.terrainCollision.size() != count || grid.objectCollision.size() != count ||
        grid.objectLightBlocked.size() != count ||
        grid.fullTerrainCollision.size() != count)
        throw std::runtime_error("Incomplete prepared collision grid");
    if (initial.activation.rooms().empty() || initial.roomNear.size() != initial.activation.rooms().size())
        throw std::runtime_error("Missing original room topology");
    for (const auto &near : initial.roomNear)
        if (near.empty()) throw std::runtime_error("Missing original near-room list");
    // Cross-area borrows must be installed by this store, never imported.
    if (!initial.collision.neighbours.empty() || !initial.collision.navigationSources.empty() ||
        std::any_of(grid.fullTerrainCollision.begin(),grid.fullTerrainCollision.end(),[](uint16_t value) {return value!=0xffff;}))
        throw std::runtime_error("Invalid prepared authority area");
    if (int(initial.id) < 1 || int(initial.id) > 255 || initial.act < 0 || initial.act > 4)
        throw std::runtime_error("Invalid native area identity");
    for(const auto &cell:initial.openedTombWall) if(cell.index>=count)
        throw std::runtime_error("Invalid prepared tomb collision patch");
}

std::vector<std::pair<RegionId,size_t>> AreaStore::loadedRoomOrder() const {
    std::vector<std::pair<RegionId,size_t>> result;
    for(const auto &[region,area]:areas_)
        for(size_t index=0;index<area.rooms.size();++index)
            if(area.rooms[index].loaded && !area.provisionalPd2Map) result.emplace_back(region,index);
    std::sort(result.begin(),result.end(),[&](const auto &first,const auto &second) {
        const int firstAct=areas_.at(first.first).definition.act,secondAct=areas_.at(second.first).definition.act;
        if(firstAct!=secondAct) return firstAct<secondAct;
        return areas_.at(first.first).rooms.at(first.second).creationOrder>
            areas_.at(second.first).rooms.at(second.second).creationOrder;
    });
    return result;
}
DomainResult<RoomRandomLoan> AreaStore::roomRandom(RoomView room) const {
    const auto *area=find(room.area);if(!area) return {DomainStatus::Unavailable,{}};
    const auto &layout=area->definition.activation;
    const auto *bounds=layout.room(Vec{float(room.x*5),float(room.y*5)}-area->definition.origin);
    if(!bounds) return {DomainStatus::Unavailable,{}};
    if((int(area->definition.origin.x)+bounds->x)/5!=room.x || (int(area->definition.origin.y)+bounds->y)/5!=room.y)
        return {DomainStatus::Unavailable,{}};
    const auto index=size_t(bounds-layout.rooms().data());
    if(index>=area->rooms.size() || !area->rooms[index].loaded) return {DomainStatus::Unavailable,{}};
    const auto &active=area->rooms[index];
    if(active.randomRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(!active.random) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,RoomRandomLoan{room,area->generation,active.randomRevision,*active.random,*active.random}};
}
bool AreaStore::validateRoomRandom(const RoomRandomLoan &loan) const noexcept {
    const auto current=roomRandom(loan.room);
    return current && current.value->areaGeneration==loan.areaGeneration && current.value->revision==loan.revision &&
        current.value->expected==loan.expected;
}
void AreaStore::publishRoomRandom(const RoomRandomLoan &loan) noexcept {
    if(!validateRoomRandom(loan)) std::terminate();
    auto &area=areas_.at(loan.room.area);
    const auto *bounds=area.definition.activation.room(Vec{float(loan.room.x*5),float(loan.room.y*5)}-area.definition.origin);
    auto &active=area.rooms.at(size_t(bounds-area.definition.activation.rooms().data()));
    if(loan.next!=loan.expected) {active.random=loan.next;++active.randomRevision;}
}
uint16_t AreaStore::nativeCollisionMask(RoomView room,int32_t x,int32_t y,int size,uint16_t mask,const CollisionSample &sample) const {
    if(size<0 || size>3 || !sample) return 0xffff;
    const auto &area=at(room.area).definition;
    const Vec anchor=Vec{float(room.x*5),float(room.y*5)}-area.origin;
    uint16_t result{};
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(nativeCollisionSizeCell(size,dx,dy)) {
        const auto cell=roomAtPosition(room.area,anchor,x+dx,y+dy);
        // COLLISION_CheckCollisionMask returns INVALID, even if those bits
        // were absent from the caller's requested mask.
        result|=cell?uint16_t(sample(*cell,x+dx,y+dy)&mask):uint16_t(0x27);
    }
    return result;
}
std::optional<RoomPoint> AreaStore::nativeFreeCoordinates(RoomView initial,int32_t x,int32_t y,int size,uint16_t mask,
    const CollisionSample &sample,std::optional<std::pair<int32_t,int32_t>> field,uint16_t fieldMask,bool fallback,int maxDistance,int increment) const {
    if(size<0 || size>3 || maxDistance<1 || !sample) return {};
    const auto find=[&](RoomView source,int32_t column,int32_t row) {
        const auto &area=at(source.area).definition;
        return roomAtPosition(source.area,Vec{float(source.x*5),float(source.y*5)}-area.origin,column,row);
    };
    auto cursor=find(initial,x,y);if(!cursor) return {};
    const auto &directions=at(initial.area).definition.collisionField;
    if(field && !directions) throw std::logic_error("Missing original expfield.d2 placement data");
    const auto clear=[&](int column,int row) {
        const auto room=find(*cursor,column,row);if(!room) return false;
        cursor=room;
        if(nativeCollisionMask(*room,column,row,size,mask,sample)) return false;
        if(field && nativeFieldBlocked(*directions,field->first,field->second,column,row,fieldMask,[&](int atX,int atY)->uint16_t {
            const auto cell=find(*room,atX,atY);return cell?sample(*cell,atX,atY):uint16_t(0xffff);
        })) return false;
        return true;
    };
    const auto found=nativeSpawnCoordinate(x,y,maxDistance,clear,increment);
    if(!found && !fallback) return {};
    const auto column=found?found->first:x,row=found?found->second:y;
    const auto room=find(*cursor,column,row);if(!room) return {};
    return RoomPoint{*room,Vec{float(column),float(row)}-at(room->area).definition.origin};
}
std::optional<RoomPoint> AreaStore::nativeFreeSpace(RoomView room,int32_t x,int32_t y,int size,const CollisionSample &sample) const {
    const auto &area=at(room.area).definition;
    const auto adjacent=roomAtPosition(room.area,Vec{float(room.x*5),float(room.y*5)}-area.origin,x+2,y+3);
    return nativeFreeCoordinates(room,adjacent?x+2:x,adjacent?y+3:y,size,0x3e01,sample,std::pair{x,y},0x801,true);
}
std::optional<RoomPoint> AreaStore::nativeRestoreCoordinates(RoomView initial,int32_t x,int32_t y,int size,uint16_t mask,
    const CollisionSample &sample) const {
    if(size<0 || size>3 || !sample) return {};
    const auto bounds=[&](RoomView view) {
        const auto &area=at(view.area).definition;
        const auto *room=area.activation.room(Vec{float(view.x*5),float(view.y*5)}-area.origin);
        if(!room) throw std::logic_error("Missing native restoration room");
        return RoomBounds{int(area.origin.x)+room->x,int(area.origin.y)+room->y,room->width,room->height};
    };
    const auto find=[&](RoomView view,int column,int row) {
        const auto &area=at(view.area).definition;
        return roomAtPosition(view.area,Vec{float(view.x*5),float(view.y*5)}-area.origin,column,row);
    };
    auto currentBounds=bounds(initial);
    int column=x;
    const int width=size+2,half=width/2;
    for(int low=-1,high=1;low>-50;--low,++high) {
        int row=nativeAdd(y,low);
        for(int offsetY=low;offsetY<high;offsetY+=2,row=nativeAdd(row,2)) {
            std::optional<RoomView> rowRoom=initial;
            if(row<currentBounds.y || row>=nativeAdd(currentBounds.y,currentBounds.height)) rowRoom=find(initial,column,row);
            if(!rowRoom) continue;
            column=nativeAdd(x,low);
            for(int offsetX=low;offsetX<high;offsetX+=2,column=nativeAdd(column,2)) {
                currentBounds=bounds(*rowRoom);
                std::optional<RoomView> candidate=*rowRoom;
                if(column<currentBounds.x || column>=nativeAdd(currentBounds.x,currentBounds.width)) candidate=find(*rowRoom,column,row);
                if(!candidate) continue;
                bool clear=true;
                for(int deltaY=0;deltaY<width && clear;++deltaY) for(int deltaX=0;deltaX<width;++deltaX) {
                    const int cellX=nativeAdd(column,deltaX-half),cellY=nativeAdd(row,deltaY-half);
                    const auto cell=find(*candidate,cellX,cellY);
                    if(!cell || (sample(*cell,cellX,cellY)&mask)) {clear=false;break;}
                }
                if(clear) return RoomPoint{*candidate,Vec{float(column),float(row)}-at(candidate->area).definition.origin};
            }
        }
    }
    return {};
}
std::optional<RoomPoint> AreaStore::nativeQuestFreePosition(RoomView initial,int32_t x,int32_t y,int size,uint16_t mask,
    int radius,const CollisionSample &sample) const {
    if(size<0 || size>3 || radius<=1 || radius>100 || !sample) return {};
    const auto bounds=[&](RoomView view) {
        const auto &area=at(view.area).definition;
        const auto *room=area.activation.room(Vec{float(view.x*5),float(view.y*5)}-area.origin);
        if(!room) throw std::logic_error("Missing native quest placement room");
        return RoomBounds{int(area.origin.x)+room->x,int(area.origin.y)+room->y,room->width,room->height};
    };
    const auto find=[&](RoomView view,int column,int row) {
        const auto &area=at(view.area).definition;
        return roomAtPosition(view.area,Vec{float(view.x*5),float(view.y*5)}-area.origin,column,row);
    };
    auto currentBounds=bounds(initial);
    const int centerX=nativeSubtract(x,size>>1),centerY=nativeSubtract(y,size>>1),width=2*size+1;
    int column=centerX;
    for(int low=-1,high=1;high<radius;--low,++high) {
        for(int offsetY=high;offsetY>low;offsetY-=2) {
            const int row=nativeAdd(centerY,offsetY);
            std::optional<RoomView> rowRoom=initial;
            if(row<currentBounds.y || row>=nativeAdd(currentBounds.y,currentBounds.height)) rowRoom=find(initial,column,row);
            if(!rowRoom) continue;
            column=nativeAdd(centerX,low);
            for(int offsetX=low;offsetX<high;offsetX+=2,column=nativeAdd(column,2)) {
                currentBounds=bounds(*rowRoom);
                std::optional<RoomView> candidate=*rowRoom;
                if(column<currentBounds.x || row>=nativeAdd(currentBounds.x,currentBounds.width)) candidate=find(*rowRoom,column,row);
                if(!candidate) continue;
                bool clear=true;
                for(int deltaY=0;deltaY<width && clear;++deltaY) for(int deltaX=0;deltaX<width;++deltaX) {
                    const int cellX=nativeAdd(column,deltaX-size),cellY=nativeAdd(row,deltaY-size);
                    const auto cell=find(*candidate,cellX,cellY);
                    if(!cell || (sample(*cell,cellX,cellY)&mask)) {clear=false;break;}
                }
                if(!clear || uint32_t(column)<uint32_t(currentBounds.x) ||
                    uint32_t(column)>=uint32_t(nativeAdd(currentBounds.x,currentBounds.width)) ||
                    uint32_t(row)<uint32_t(currentBounds.y) ||
                    uint32_t(row)>=uint32_t(nativeAdd(currentBounds.y,currentBounds.height))) continue;
                const int resultX=nativeAdd(column,size>>1),resultY=nativeAdd(row,size>>1);
                const auto room=find(initial,resultX,resultY);if(!room) return {};
                return RoomPoint{*room,Vec{float(resultX),float(resultY)}-at(room->area).definition.origin};
            }
        }
    }
    return {};
}
void AreaStore::initializeUnitRooms(RegionId region) {
    auto &area=areas_.at(region);
    if(area.unitHeads.empty()) area.unitHeads.resize(area.definition.activation.rooms().size());
    auto &grid=area.definition.collision;
    if(grid.missileCollision.empty()) grid.missileCollision.resize(size_t(grid.width)*grid.height);
    if(grid.nativeCollision.empty()) {
        grid.nativeCollision.resize(size_t(grid.width)*grid.height);
        for(size_t cell=0;cell<grid.nativeCollision.size();++cell)
            grid.nativeCollision[cell]=(grid.fullTerrainCollision.empty()?grid.terrainCollision[cell]:grid.fullTerrainCollision[cell]) |
                grid.objectCollision[cell] | grid.missileCollision[cell] |
                (grid.blocked[cell] && !(grid.terrainCollision[cell]&9)?1:0);
    }
}
void AreaStore::setMissileMask(RegionId source,Vec position,int size,bool occupied) noexcept {
    if(size<1 || size>3) return;
    const auto found=areas_.find(source);if(found==areas_.end()) return;
    const auto origin=found->second.definition.origin;
    const auto x=int32_t(std::floor(position.x+origin.x)),y=int32_t(std::floor(position.y+origin.y));
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!nativeCollisionSizeCell(size,dx,dy)) continue;
        const auto room=roomAtPosition(source,position,x+dx,y+dy);if(!room) continue;
        auto &area=areas_.at(room->area);auto &grid=area.definition.collision;
        const auto column=x+dx-int32_t(area.definition.origin.x),row=y+dy-int32_t(area.definition.origin.y);
        if(column<0 || row<0 || column>=grid.width || row>=grid.height) continue;
        const auto index=size_t(row)*grid.width+column;
        if(index>=grid.missileCollision.size()) std::terminate();
        const auto previous=grid.missileCollision[index];
        // Native Set/ResetMask are bit operations, not overlapping-unit counts.
        grid.missileCollision[index]=occupied?uint16_t(previous|0x40):uint16_t(previous&~0x40);
        if(previous!=grid.missileCollision[index]) ++grid.missileCollisionRevision;
        const auto native=grid.nativeCollision[index];
        writeNativeCell(room->area,index,occupied?uint16_t(native|0x40):uint16_t(native&~0x40));
    }
}
void AreaStore::setNativePatternMask(RegionId source,Vec position,NativeCollisionPattern pattern,uint16_t mask,bool occupied) noexcept {
    const auto found=areas_.find(source);if(found==areas_.end()) return;
    const auto global=position+found->second.definition.origin;
    const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        const auto bits=nativeCollisionFootprint(pattern,dx,dy,mask);if(!bits) continue;
        const auto room=roomAtPosition(source,position,x+dx,y+dy);if(!room) continue;
        auto &area=areas_.at(room->area);auto &grid=area.definition.collision;
        const int column=x+dx-int(area.definition.origin.x),row=y+dy-int(area.definition.origin.y);
        if(column<0 || row<0 || column>=grid.width || row>=grid.height) continue;
        const auto index=size_t(row)*grid.width+column;
        if(index>=grid.missileCollision.size()) std::terminate();
        const auto previous=grid.missileCollision[index];
        grid.missileCollision[index]=occupied?uint16_t(previous|bits):uint16_t(previous&~bits);
        if(previous!=grid.missileCollision[index]) ++grid.missileCollisionRevision;
        const auto native=grid.nativeCollision[index];
        writeNativeCell(room->area,index,occupied?uint16_t(native|bits):uint16_t(native&~bits));
    }
}
std::optional<std::pair<RegionId,size_t>> AreaStore::unitRoom(RegionId region,Vec point) const noexcept {
    const auto *area=find(region);if(!area) return {};
    const auto *room=area->definition.activation.room(point);if(!room) return {};
    return std::pair{region,size_t(room-area->definition.activation.rooms().data())};
}
std::shared_ptr<RoomUnitRegistration> AreaStore::prepareRoomUnit(EntityId id,uint8_t kind,RegionId region,Vec point,std::optional<RoomView> nativeRoom) const {
    if(!id || kind>5 || roomUnits_.contains(id)) throw std::invalid_argument("Invalid room unit registration");
    auto room=unitRoom(region,point);
    if(nativeRoom) {
        if(nativeRoom->area!=region) throw std::invalid_argument("Native room belongs to another area");
        const auto *area=find(region);if(!area) throw std::invalid_argument("Missing native room area");
        const auto &rooms=area->definition.activation.rooms();
        const auto found=std::find_if(rooms.begin(),rooms.end(),[&](const auto &bounds) {
            return (int(area->definition.origin.x)+bounds.x)/5==nativeRoom->x && (int(area->definition.origin.y)+bounds.y)/5==nativeRoom->y;
        });
        if(found==rooms.end()) throw std::invalid_argument("Unknown native admission room");
        const auto index=size_t(found-rooms.begin());
        if(index>=area->rooms.size() || !area->rooms[index].loaded) throw std::invalid_argument("Native admission room is not loaded");
        room=std::pair{region,index};
    }
    if(!room) throw std::runtime_error("Unit admission has no native room");
    std::map<EntityId,RoomUnitEntry> prepared;prepared.emplace(id,RoomUnitEntry{kind,room,{},{}});
    auto result=std::make_shared<RoomUnitRegistration>();result->node=prepared.extract(id);return result;
}
void AreaStore::linkRoomUnit(EntityId id,RoomUnitEntry &entry) noexcept {
    if(!entry.room) return;
    auto &head=areas_.at(entry.room->first).unitHeads.at(entry.room->second);
    entry.previous={};entry.next=head;
    if(head) roomUnits_.at(head).previous=id;
    head=id;
}
void AreaStore::unlinkRoomUnit(EntityId,RoomUnitEntry &entry) noexcept {
    if(!entry.room) return;
    if(entry.previous) roomUnits_.at(entry.previous).next=entry.next;
    else areas_.at(entry.room->first).unitHeads.at(entry.room->second)=entry.next;
    if(entry.next) roomUnits_.at(entry.next).previous=entry.previous;
    entry.previous={};entry.next={};
}
void AreaStore::publishRoomUnit(RoomUnitRegistration &prepared) noexcept {
    if(prepared.publishedBy_) {
        if(!validateRoomUnit(prepared,prepared.publishedId_)) std::terminate();
        return;
    }
    if(prepared.node.empty() || roomUnits_.contains(prepared.node.key())) std::terminate();
    const auto inserted=roomUnits_.insert(std::move(prepared.node));
    linkRoomUnit(inserted.position->first,inserted.position->second);
    prepared.publishedBy_=this;prepared.publishedId_=inserted.position->first;
}
bool AreaStore::validateRoomUnit(const RoomUnitRegistration &prepared,EntityId id) const {
    if(prepared.publishedBy_) return prepared.publishedBy_==this && prepared.publishedId_==id && roomUnits_.contains(id);
    return !prepared.node.empty() && prepared.node.key()==id && !roomUnits_.contains(id);
}
void AreaStore::moveRoomUnit(EntityId id,RegionId region,Vec point) noexcept {
    const auto found=roomUnits_.find(id);if(found==roomUnits_.end()) std::terminate();
    auto body=found->second.body;
    if(body.mask) {
        const auto global=point+at(region).definition.origin;
        body.area=region;body.x=int32_t(std::floor(global.x+.5f));body.y=int32_t(std::floor(global.y+.5f));
        setUnitCollision(id,body,found->second.reservation,found->second.collisionAlive);
    }
    const auto room=unitRoom(region,point);auto &entry=found->second;
    if(room==entry.room) return;
    unlinkRoomUnit(id,entry);entry.room=room;linkRoomUnit(id,entry);
}
void AreaStore::removeRoomUnit(EntityId id) noexcept {
    const auto found=roomUnits_.find(id);if(found==roomUnits_.end()) return;
    writeFootprint(found->second.body,false);writeFootprint(found->second.reservation,false);
    unlinkRoomUnit(id,found->second);roomUnits_.erase(found);
}
std::vector<EntityId> AreaStore::roomUnitOrder(RoomView view) const {
    const auto *area=find(view.area);if(!area) return {};
    const auto &rooms=area->definition.activation.rooms();
    const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &bounds) {
        return (int(area->definition.origin.x)+bounds.x)/5==view.x && (int(area->definition.origin.y)+bounds.y)/5==view.y;
    });
    if(room==rooms.end()) return {};
    std::vector<EntityId> order;
    for(auto id=area->unitHeads.at(size_t(room-rooms.begin()));id;id=roomUnits_.at(id).next) order.push_back(id);
    return order;
}
std::optional<NativeActEnvironment> AreaStore::environment(RegionId region) const {
    const auto *area=find(region);if(!area) return {};
    const auto &time=actTimes_.at(size_t(area->definition.act));return time?std::optional{time->value}:std::nullopt;
}
std::optional<StatTimeContext> AreaStore::statTime(RegionId region) const {
    const auto *area=find(region);if(!area) return {};
    const auto act=area->definition.act;
    if(act<0 || act>=5) throw std::runtime_error("Invalid prepared area act");
    const auto &time=actTimes_.at(size_t(act));return time?std::optional{actStatTime(time->value)}:std::nullopt;
}
uint64_t AreaStore::environmentRevision(int act) const {
    if(act<0 || act>=5) throw std::invalid_argument("Invalid environment revision act");
    const auto &time=actTimes_.at(size_t(act));return time?time->revision:0;
}
bool AreaStore::loaded(RegionId id, Vec point) const {
    const auto *area = find(id);
    if (!area) return false;
    const auto &layout = area->definition.activation;
    const auto *room = layout.room(point);
    if (!room) return false;
    return !area->rooms.empty() && area->rooms.at(size_t(room - layout.rooms().data())).loaded;
}
bool AreaStore::active(RegionId id, Vec point) const {
    const auto *area = find(id);
    if (!area) return false;
    const auto &layout = area->definition.activation;
    const auto *room = layout.room(point);
    if (!room) return false;
    if (area->rooms.empty()) return false;
    const auto &state = area->rooms.at(size_t(room - layout.rooms().data()));
    return state.loaded && state.status <= 1;
}
bool AreaStore::nearby(RegionId from, Vec observer, RegionId to, Vec target) const {
    const auto *source = find(from), *destination = find(to);
    if (!source || !destination || source->definition.act != destination->definition.act) return false;
    const auto *first = source->definition.activation.room(observer);
    const auto *second = destination->definition.activation.room(target);
    if (!first || !second || !loaded(to, target)) return false;
    const auto index = size_t(first - source->definition.activation.rooms().data());
    const RoomView desired{to, (int(destination->definition.origin.x) + second->x) / 5,
        (int(destination->definition.origin.y) + second->y) / 5};
    const auto &near = source->definition.roomNear.at(index);
    return std::find(near.begin(), near.end(), desired) != near.end();
}
std::optional<RoomView> AreaStore::roomAtPosition(RegionId source,Vec observer,int32_t x,int32_t y) const {
    const auto *area=find(source);if(!area) return {};
    const auto &definition=area->definition;
    const auto *origin=definition.activation.room(observer);if(!origin) return {};
    const auto contains=[&](RoomView view)->bool {
        const auto *target=find(view.area);if(!target) return false;
        const auto &layout=target->definition;
        const auto &rooms=layout.activation.rooms();
        const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &bounds) {
            return (int(layout.origin.x)+bounds.x)/5==view.x && (int(layout.origin.y)+bounds.y)/5==view.y;
        });
        if(room==rooms.end() || target->rooms.empty() || !target->rooms.at(size_t(room-rooms.begin())).loaded) return false;
        const int left=int(layout.origin.x)+room->x,top=int(layout.origin.y)+room->y;
        return x>=left && x<left+room->width && y>=top && y<top+room->height;
    };
    // DUNGEON_GetRoomAtPosition checks the current room, then its authored
    // adjacent-room array; it never searches every room in the act.
    const RoomView current{source,(int(definition.origin.x)+origin->x)/5,(int(definition.origin.y)+origin->y)/5};
    const auto index=size_t(origin-definition.activation.rooms().data());
    const int left=int(definition.origin.x)+origin->x,top=int(definition.origin.y)+origin->y;
    // The current RoomBounds is already resolved by RoomLayout's cell index.
    // D2MOO DungeonTestRoomGame uses this same half-open bounds test directly;
    // do not rediscover its anchor with a linear scan for every footprint cell.
    if(!area->rooms.empty() && area->rooms.at(index).loaded &&
       x>=left && x<left+origin->width && y>=top && y<top+origin->height) return current;
    for(const auto view:definition.roomNear.at(index))
        if(contains(view)) return view;
    return {};
}
}
