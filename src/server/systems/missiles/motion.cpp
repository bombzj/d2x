#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::missiles {
NativeMissileMotionResult System::moveMissile(Missile &missile) const {
    const auto &area=ports_.areas.at(missile.area);const auto origin=area.definition.origin;
    const auto velocity=int32_t(std::lround(missile.velocity.length()*4096.f/25.f));
    const bool waypoint=!missile.path.empty();
    while(!missile.path.empty() && missile.path.front().x==missile.position.x && missile.path.front().y==missile.position.y) missile.path.pop_front();
    if(waypoint && missile.path.empty()) return {missile.position,false,true,false,true};
    const auto target=missile.path.empty()?missile.pathTarget:missile.path.front();
    const auto collision=[&](int32_t x,int32_t y) {
        uint16_t mask{};
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(missile.collision.size<=1 && (dx || dy)) continue;
            if(missile.collision.size==2 && dx && dy) continue;
            const auto gx=int32_t(origin.x)+x+dx,gy=int32_t(origin.y)+y+dy;
            const auto room=ports_.areas.roomAtPosition(missile.area,missile.position,gx,gy);
            if(!room) {mask|=5;continue;}
            const auto &region=ports_.areas.at(room->area).definition;
            mask|=region.collision.movementMask(gx-int32_t(region.origin.x),gy-int32_t(region.origin.y))&missile.collision.mask;
        }
        return mask;
    };
    auto result=missile.motion.advance(missile.position,target,velocity,waypoint?NativeMissileMotion::Path::Waypoint:NativeMissileMotion::Path::Classic,
        missile.pathRevision,collision);
    if(waypoint && result.reached && !missile.path.empty()) {missile.path.pop_front();result.pathFinished=missile.path.empty();}
    missile.movedCell=result.movedCell;
    return result;
}
std::vector<std::pair<float,EntityId>> System::collisionContacts(const Missile &missile,Vec next) const {
    // MISSMODE_HandleMissileCollision checks mode, activation and saved PATH
    // steps before finding units. These gates and candidate filters are read-only.
    const auto steps=missile.motion.steps();
    const auto mask=missile.nativeDamage?nativeMissileContactMask(missile.nativeDamage->rule.collisionMode):uint16_t{};
    if(steps.empty() || (missile.nativeDamage && (missile.nativeDamage->rule.collisionMode==6 ||
        !mask || missile.currentFrame()>missile.nativeActivateFrame))) return {};
    const auto roomUnits=nearbyRoomUnits(missile,next);
    std::vector<EntityId> candidates;
    bool candidatesPrepared{};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto *area=ports_.areas.find(missile.area);if(!area) return {};
    const auto global=next+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(missile.area,missile.position,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return {};
    const auto sample=mask?ports_.spatial.nativeCollisionSample(mask,std::span<const EntityId>{roomUnits}):AreaStore::CollisionSample{};
    size_t step{};
    for(const auto cell:steps) {
        uint16_t collided{};
        if(missile.nativeDamage) {
            collided=ports_.areas.nativeCollisionMask(*room,int32_t(std::floor(cell.x+area->definition.origin.x)),
                int32_t(std::floor(cell.y+area->definition.origin.y)),missile.collision.size,mask,sample);
            if(!collided) {++step;continue;}
        }
        // As in the native mask gate, empty path cells need no unit predicates.
        // Keep the original room-chain order and sample only within this call.
        if(!candidatesPrepared) {
            candidates.reserve(roomUnits.size());
            for(const auto id:roomUnits) if(canCollide(missile,id) && unitPosition(missile,id)) candidates.push_back(id);
            candidatesPrepared=true;
        }
        for(const auto id:candidates) {
            const auto target=participants.find(id);const auto position=unitPosition(missile,id);
            const bool overlaps=position && (missile.nativeDamage?
                nativeMissileUnitOverlap(missile.collision.size,target.size(),int(std::floor(cell.x))-int(std::floor(position->x)),
                    int(std::floor(cell.y))-int(std::floor(position->y))):bool(missileUnitIntersection(cell,cell,missile.collision.size,*position,target.size())));
            if(overlaps)
                return {{float(step),id}};
        }
        if(collided&4) return {{float(step),EntityId{}}};
        ++step;
    }
    return {};
}
}
