#include "../system.hpp"
#include "../native_pet_move.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>

namespace d2x::server::companions {
struct Pd2ShadowFollow {
    std::vector<monsters::MoveRequest> attempts;
    size_t next{};
    int nativeMotion{-1};
    bool neutralOnFailure{},wander{};
};
namespace {
int squared(Vec a,Vec b) {const int x=int(a.x)-int(b.x),y=int(a.y)-int(b.y);return x*x+y*y;}
}
DomainResult<bool> followPd2Shadow(const Ports &ports,Companion &pet,const PlayerState &owner,
    const monsters::Actor &body,EntityId target,uint64_t tick,const NativePetMovement &options) {
    const auto &area=ports.areas.at(body.area),&ownerArea=ports.areas.at(owner.area);
    const auto ownerPoint=owner.position+ownerArea.definition.origin-area.definition.origin;
    if(!pet.pd2ShadowFollow) {
        auto plan=std::make_shared<Pd2ShadowFollow>();
        if(!target) {
            const auto candidate=ports.monsters.nativeTargetPosition(body.movementTarget,body.area);
            if(body.movementTarget!=owner.actor && candidate && squared(body.position,candidate->first)<=1155)
                target=body.movementTarget;
        }
        const int ownerDistance=squared(body.position,ownerPoint);
        const auto goal=owner.routeDestination+ownerArea.definition.origin-area.definition.origin;
        const bool heading=owner.moving && (int(goal.x+area.definition.origin.x) || int(goal.y+area.definition.origin.y));
        const int distance=std::max(ownerDistance,heading?squared(body.position,goal):0);
        const auto move=[&](Vec point,EntityId unit,int velocity,bool running) {
            monsters::MoveRequest request{body.id,{body.area,area.generation,point},unit,0,velocity,running,1};
            request.pathType=15;request.pathDistance=40;plan->attempts.push_back(request);
        };
        auto random=body.combatRandom;
        if(double(distance)>(target?3.2:1.0)*361.0) {
            if(ownerDistance>=2304) plan->nativeMotion=3;
            else {
                int dx{},dy{};
                if(heading) do {dx=int(pd2UnitEventRandom(random)%6)-3;dy=int(pd2UnitEventRandom(random)%6)-3;}
                    while(dx>=-1 && dx<=1 && dy>=-1 && dy<=1);
                const bool running=body.rule.curseAiRunMode;
                const auto stats=ports.effects.unitStatValues(owner.actor,tick);
                if(!stats) return {DomainStatus::Capacity,{}};
                const int velocity=75+(uint32_t(nativeStatValue(*stats,*owner.rules.skills->stats,"item_fastermovevelocity"))>>1)+(running?20:40);
                move(heading?goal+Vec{float(dx),float(dy)}:ownerPoint,heading?EntityId{}:owner.actor,velocity,running);
                if(unitCoordinateDistance(int(body.position.x),int(body.position.y),int(ownerPoint.x),int(ownerPoint.y))>=3)
                    move(Vec{float((int(body.position.x)+int(ownerPoint.x))/2),float((int(body.position.y)+int(ownerPoint.y))/2)},{},velocity,running);
                plan->neutralOnFailure=true;
            }
        } else if(!target) {
            if(distance<6) plan->nativeMotion=5;
            else if(pd2UnitEventRandom(random)%12==0) {
                plan->wander=true;
                int x{},y{},attempt{},radius=INT32_MAX;
                for(; (radius<6 || radius>=361) && attempt<10; ++attempt) {
                    while(x>-3 && x<3 && y>=-2 && y<=2) {
                        x=int(pd2UnitEventRandom(random)%10)-5;y=int(pd2UnitEventRandom(random)%10)-5;
                    }
                    x+=int(body.position.x+area.definition.origin.x);y+=int(body.position.y+area.definition.origin.y);
                    const int ox=int(ownerPoint.x+area.definition.origin.x)-x,oy=int(ownerPoint.y+area.definition.origin.y)-y;
                    radius=ox*ox+oy*oy;
                }
                if(attempt!=10) {
                    move(Vec{float(x),float(y)}-area.definition.origin,{},75,false);
                    plan->attempts.back().pathType=body.pathType;
                    plan->attempts.back().pathDistance=body.pathDistance;
                }
            }
        }
        const auto consumed=ports.monsters.consumeRandom(body.id,body.combatRandom,random);
        if(!consumed) return {consumed.status,{}};
        pet.pd2ShadowFollow=std::move(plan);
    }
    const auto plan=pet.pd2ShadowFollow;
    if(plan->nativeMotion>=0) {
        auto native=options;native.motion=plan->nativeMotion;
        if(native.motion==5) {native.velocityBonus=2;native.spacing=5;}
        const bool moved=followNativePet(ports,pet,owner,body,target,false,tick,native);
        pet.pd2ShadowFollow.reset();return {DomainStatus::Applied,moved};
    }
    if(plan->wander) {ports.monsters.neutralMode(body.id);pet.nextDecision=tick+1;plan->wander=false;}
    while(plan->next<plan->attempts.size()) {
        const auto moved=ports.monsters.requestMove(plan->attempts[plan->next]);
        if(isDeferred(moved.status)) return {moved.status,{}};
        ++plan->next;
        if(moved) {pet.nativePetMoving=true;pet.pd2ShadowFollow.reset();return {DomainStatus::Applied,true};}
    }
    if(plan->neutralOnFailure) {ports.monsters.neutralMode(body.id);pet.nextDecision=tick+1;}
    pet.pd2ShadowFollow.reset();return {DomainStatus::Applied,false};
}
}
