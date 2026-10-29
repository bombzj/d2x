#include "native_pet_move.hpp"
#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/units/direction.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
namespace d2x::server::companions {
namespace {
struct PetLogical {const RoomPopulationLayout *layout{};size_t region{};int index{};};
PetLogical petLogical(const AreaState &area,Vec point) {
    const auto *room=area.definition.activation.room(point);if(!room) return {};
    const auto at=size_t(room-area.definition.activation.rooms().data());if(at>=area.definition.populationRooms.size()) return {};
    const auto &layout=area.definition.populationRooms[at];const auto global=point+area.definition.origin;
    const int x=int(global.x)/5-layout.x,y=int(global.y)/5-layout.y;
    if(x<0 || y<0 || x>layout.width || y>layout.height || layout.regions.empty()) return {};
    const auto index=layout.single?size_t{}:layout.cells.at(size_t(y)*(layout.width+1)+x);
    return {&layout,index,layout.regions.at(index).index};
}
}
bool followNativePet(const Ports &ports,Companion &pet,const PlayerState &owner,const monsters::Actor &body,EntityId target,bool combat,uint64_t tick,const NativePetMovement &options) {
    const auto &area=ports.areas.at(body.area);const auto &ownerArea=ports.areas.at(owner.area);
    const auto ownerPoint=owner.position+ownerArea.definition.origin-area.definition.origin;
    const int distance=unitCoordinateDistanceWithSize(int(body.position.x),int(body.position.y),int(ownerPoint.x),int(ownerPoint.y),body.rule.size);
    int count{};for(const auto &[id,entry]:ports.monsters.read().actors) {(void)id;if(entry.owner==owner.player && entry.alive()) ++count;}
    if(options.registeredCount) count=int(ports.monsters.pets(owner.player,true).size());
    const auto bound=std::min((count>>1)+options.spacing,36);
    const bool neutral=!ownerArea.definition.town && !owner.moving && !ports.skills.read().busy(owner.actor,tick);
    if(options.motion<0 && (distance>1 || !neutral || combat) && target && !area.definition.town && distance<=80) return false;
    const auto ownerLogical=petLogical(ownerArea,owner.position),unitLogical=petLogical(area,body.position);
    int motion=owner.moving?0:2;
    if(ownerLogical.index!=unitLogical.index || distance>bound) motion=1;
    if(distance>50) motion=3;
    const auto lastWarp=owner.lastTeleportPosition-area.definition.origin;
    if(unitCoordinateDistance(int(body.position.x),int(body.position.y),int(lastWarp.x),int(lastWarp.y))<28 &&
       ((motion==1 && distance<30) || motion==2)) motion=4;
    if(distance<=1 && neutral && !combat) motion=5;
    if(options.motion>=0) motion=options.motion;
    const auto move=[&](Vec point,int velocity=75,int path=13,int maximum=100,bool run=false,EntityId unit={}) {
        monsters::MoveRequest request{body.id,{body.area,area.generation,point},unit,0,velocity+options.velocityBonus,run || options.running};
        request.pathType=uint8_t(path);request.pathDistance=uint8_t(maximum);request.pathSteps=1;
        const bool moved=bool(ports.monsters.requestMove(request));if(moved) pet.nativePetMoving=true;return moved;
    };
    auto random=body.combatRandom;
    const auto wander=[&](int radius,int velocity=75,int path=13) {const auto point=monsterWanderPoint(ownerPoint,radius,random)-Vec{.5f,.5f};ports.monsters.commitCombatRandom(body.id,random);return move(point,velocity,path);};
    const auto escape=[&](int radius,int path=13) {
        const Vec direction{float((body.position.x>ownerPoint.x)-(body.position.x<ownerPoint.x)),float((body.position.y>ownerPoint.y)-(body.position.y<ownerPoint.y))};
        return move(body.position+direction*float(radius),75,path,radius>5?radius:100);
    };
    if(motion==3) {
        const auto global=owner.position+ownerArea.definition.origin;
        const auto room=ports.areas.roomAtPosition(owner.area,owner.position,int(global.x),int(global.y));
        if(!room) return false;
        const auto *bounds=ownerArea.definition.activation.room(owner.position);if(!bounds) return false;
        int left=int(ownerArea.definition.origin.x)+bounds->x+1,top=int(ownerArea.definition.origin.y)+bounds->y+1;
        int width=bounds->width-1,height=bounds->height-1;
        if(ownerLogical.layout) {
            const auto &box=ownerLogical.layout->regions.at(ownerLogical.region);
            left=box.left*5+1;top=box.top*5+1;width=box.right*5-left;height=box.bottom*5-top;
        }
        auto rule=body.rule;rule.nativeClass=options.probeClass;
        rule.spawnCollision=options.probeCollision;rule.spawnAuxiliary.reset();rule.nativeSpawnFloor=false;rule.enchantment.reset();
        std::optional<Vec> destination;
        // 1.13c 6FC67450 admits probes even with a7=0. The older D2MOO
        // decompilation has the opposite boolean on that branch.
        for(int attempt=0;attempt<20;++attempt) {
            auto loan=ports.areas.roomRandom(*room);if(!loan) return false;
            const int x=left+int(limitedRandom(loan.value->next,uint32_t(std::max(0,width))));
            const int y=top+int(limitedRandom(loan.value->next,uint32_t(std::max(0,height))));
            const Vec requested=Vec{float(x),float(y)}-ownerArea.definition.origin;
            if(ownerLogical.layout && petLogical(ownerArea,requested).index!=ownerLogical.index) {
                if(!ports.transactions.consumeRoomRandom(*loan.value)) return false;
                continue;
            }
            monsters::Admission admission;admission.identity.spawnKey="pet-move-probe";admission.area=owner.area;admission.position=requested;admission.rule=rule;admission.tick=tick;
            const auto probe=ports.monsters.prepareRoomSpawn(admission,*loan.value,-1,ownerLogical.layout?std::optional{ownerLogical.region}:std::nullopt,true,ports.spatial.nativeCollisionSample());
            if(!probe || !(*probe.value)->validate()) return false;
            destination=(*probe.value)->position();(*probe.value)->publish();
            if(destination) break;
        }
        if(!destination) return false;
        const auto cell=*destination+ownerArea.definition.origin;
        const auto point=ports.areas.nativeFreeCoordinates(*room,int(cell.x),int(cell.y),body.rule.size,0x1c09,ports.spatial.nativeCollisionSample(0x1c09));
        if(!point || point->room.area!=owner.area) return false;
        const ActorContext context{owner.player,owner.actor,owner.area,ownerArea.generation,0,tick};
        if(!ports.monsters.warpPet(body.id,context,point->position)) return false;
        pet.nextDecision=tick+5;return true;
    }
    if(motion==1) {
        const auto destination=owner.routeDestination+ownerArea.definition.origin-area.definition.origin;
        if(owner.moving && !owner.runningNow && !ownerArea.definition.town && (move(destination) || move((body.position+destination)*.5f))) return true;
        const int velocity=int(rollRandom(random)%40)+40;ports.monsters.commitCombatRandom(body.id,random);
        bool straight{};
        for(auto point=owner.movementHistory.rbegin();point!=owner.movementHistory.rend();++point) {
            if(!point->x || !point->y) continue;
            const auto local=*point-area.definition.origin;
            if(unitCoordinateDistance(int(body.position.x),int(body.position.y),int(local.x),int(local.y))<=5) continue;
            if(move(local,75+velocity) || move(local,75+velocity,15)) return true;
            if(!straight) {straight=true;if(move(local,75+velocity,7)) return true;}
        }
        if(wander(std::max(distance/4,4),90)) return true;
        return false;
    }
    if(motion==2) {
        if(options.motion<0 && options.suppressIdle) return false;
        const bool idle=rollRandom(random)%100>=10;ports.monsters.commitCombatRandom(body.id,random);
        if(idle) {pet.nextDecision=tick+15;return true;}
        const int radius=int(rollRandom(random)%3)+3;ports.monsters.commitCombatRandom(body.id,random);
        if(!wander(radius) && !move(owner.routeDestination+ownerArea.definition.origin-area.definition.origin,75,13,40)) pet.nextDecision=tick+15;
        return true;
    }
    if(motion==4) {
        bool crowded{};
        for(const auto &[id,entry]:ports.monsters.read().actors)
            if(id!=body.id && entry.owner==owner.player && entry.alive() && entry.area==body.area &&
               unitCoordinateDistanceWithSize(int(body.position.x),int(body.position.y),int(entry.position.x),int(entry.position.y),entry.rule.size)<=1) {crowded=true;break;}
        if(!crowded || (!escape(bound) && !wander(bound))) pet.nextDecision=tick+15;
        return true;
    }
    if(motion==5) {
        return wander(bound,75,7) || escape(bound,7) || move(owner.routeDestination+ownerArea.definition.origin-area.definition.origin,75,13,40);
    }
    const auto destination=owner.routeDestination+ownerArea.definition.origin-area.definition.origin;
    const auto heading=nativeDirectionVector(int(destination.x)-int(ownerPoint.x),int(destination.y)-int(ownerPoint.y));
    auto facing=nativeOverlayDirection(heading.index);
    constexpr std::array<int,8> offsets{4,3,2,1,0,7,6,5};
    for(int attempt=0;attempt<7;++attempt,facing=uint8_t((facing+1)&7)) {
        const auto point=destination+monsterDirectionOffset(offsets[facing])*8.f;
        if(petLogical(ownerArea,point+area.definition.origin-ownerArea.definition.origin).index==ownerLogical.index &&
           (move(point,75,13,40) || move(Vec{float((int(point.x)+int(body.position.x))/2),float((int(point.y)+int(body.position.y))/2)},75,13,40))) return true;
    }
    const Vec midpoint{float((int(body.position.x)+int(ownerPoint.x))/2),float((int(body.position.y)+int(ownerPoint.y))/2)};
    return wander(4) || move(midpoint,75,13,40) || move(ownerPoint,75,13,40);
}
}
