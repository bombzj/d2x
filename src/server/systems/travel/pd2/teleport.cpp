#include "../system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x::server::travel {
DomainResult<Vec> System::pd2TeleportLanding(RegionId region,Vec source,Vec target,int size) const {
    const auto *entry=ports_.areas.find(region);
    if(!entry || size<0 || size>3 || !std::isfinite(source.x) || !std::isfinite(source.y) ||
       !std::isfinite(target.x) || !std::isfinite(target.y)) return {DomainStatus::InvalidRequest,{}};
    const auto &area=entry->definition;
    if(!area.teleportMode) return {DomainStatus::Unavailable,{}};
    if(area.teleportMode==2 && !area.collision.interactionSegment(source,target,2,{},0x804,size))
        return {DomainStatus::Unavailable,{}};
    if(area.teleportMode!=3) return {DomainStatus::Applied,target};
    const auto *bounds=area.activation.room(source+Vec{.5f,.5f});
    if(!bounds) return {DomainStatus::Unavailable,{}};
    const RoomView current{region,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    const auto cell=[](Vec at) {return Vec{std::floor(at.x+.5f),std::floor(at.y+.5f)};};
    const auto from=cell(source+area.origin);
    auto destination=cell(target+area.origin);
    const auto sample=ports_.spatial.nativeCollisionSample(0x80d);
    // 36B570 ignores a failed preliminary search; the original coordinates
    // then continue into blocker checks and the final SUNIT relocation.
    if(const auto free=ports_.areas.nativeFreeCoordinates(current,int(destination.x),int(destination.y),size,0x80d,sample))
        destination=free->position+ports_.areas.at(free->room.area).definition.origin;
    const auto landing=ports_.areas.roomAtPosition(region,source,int(destination.x),int(destination.y));
    if(!landing) return {DomainStatus::Unavailable,{}};
    std::array<const objects::Object *,3> blockers{};
    std::array<int,3> distances{999999,999999,999999};
    const auto collect=[&](RoomView center) {
        const auto &definition=ports_.areas.at(center.area).definition;
        const auto *room=definition.activation.room(Vec{float(center.x*5),float(center.y*5)}-definition.origin);
        if(!room) return;
        const auto index=size_t(room-definition.activation.rooms().data());
        // 37E170 concatenates both near-room arrays, including duplicates,
        // and preserves the first equal-distance object in native unit order.
        for(const auto near:definition.roomNear.at(index)) for(const auto id:ports_.areas.roomUnitOrder(near)) {
            const auto found=ports_.objects.read().objects.find(id);
            if(found==ports_.objects.read().objects.end()) continue;
            const auto &object=found->second;
            if(object.definition<613 || object.definition>615) continue;
            const auto at=cell(object.position+ports_.areas.at(object.area).definition.origin);
            const int reduction=size/2+object.rule.width/2;
            const int dx=std::max(int(std::abs(at.x-from.x))-reduction,0);
            const int dy=std::max(int(std::abs(at.y-from.y))-reduction,0);
            const int distance=2*std::max(dx,dy)+std::min(dx,dy); // Common10522.
            const auto slot=size_t(object.definition-613);
            if(distance<distances[slot]) {distances[slot]=distance;blockers[slot]=&object;}
        }
    };
    collect(current);collect(*landing);
    const auto outside=[](float barrier,float first,float second) {
        return (barrier<first && barrier<second) || (barrier>first && barrier>second);
    };
    bool checkCollision=false;
    for(size_t slot=0;slot<blockers.size();++slot) if(const auto *object=blockers[slot]) {
        const auto at=cell(object->position+ports_.areas.at(object->area).definition.origin);
        if(slot==0) checkCollision|=distances[0]<25 &&
            (!outside(at.x,from.x,destination.x) || !outside(at.y,from.y,destination.y));
        else if(slot==1) checkCollision|=!outside(at.x,from.x,destination.x) && std::abs(at.y-from.y)<25;
        else checkCollision|=!outside(at.y,from.y,destination.y) && std::abs(at.x-from.x)<25;
    }
    target=destination-area.origin;
    if(checkCollision && (ports_.areas.nativeCollisionMask(*landing,int(destination.x),int(destination.y),3,0x80d,sample) ||
        !area.collision.interactionSegment(source,target,2,{},0x80d,size))) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,target};
}
}
