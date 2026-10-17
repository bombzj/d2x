#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::vultureFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &bird=*ports_.monsters.find(id);const auto &p=bird.rule.ai.params;
    const auto &area=ports_.areas.at(bird.area);
    const int phase=controller.phase;
    const auto chance=[&](int percent) {return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames) {controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));};
    const auto move=[&](Vec goal,uint8_t path=13) {
        const auto &current=ports_.areas.at(bird.area);
        goal=goal+area.definition.origin-current.definition.origin;
        controller.pursuing=bool(ports_.monsters.requestMove({id,{bird.area,current.generation,goal},{},0,75,false,1,0,0,path}));
    };
    const auto radius=[&](Vec center,int radius,int distance) {
        const auto position=bird.position+ports_.areas.at(bird.area).definition.origin-area.definition.origin;
        move(monsterRadiusApproachPoint(position,bird.rule.size,center,radius,distance));
    };
    const auto pose=[&](uint8_t mode,bool flying) {
        return ports_.monsters.aiPose(id,mode,flying,flying,tick.tick,12,true);
    };
    const auto land=[&](Vec center)->DomainResult<bool> {
        const auto landed=ports_.monsters.restoreGroundPresence(id,ports_.spatial.nativeCollisionSample());
        if(!landed || !*landed.value) return landed;
        radius(center,2,3);const auto shown=pose(9,false);
        if(!shown) return {shown.status,{}};
        controller.phase=-1;idle(12);return {DomainStatus::Applied,true};
    };
    // Fn023 retains the callback's original phase even if a failed landing
    // later writes the next AI parameter. Null-unit coordinates are global 0.
    if(!target) {
        if(phase<1) {idle(12);return StepStatus::Complete;}
        const auto landed=land(bird.position);
        if(!landed) return isDeferred(landed.status)?StepStatus::Blocked:StepStatus::Complete;
        if(*landed.value) return StepStatus::Complete;
    }
    const int dx=int(point.x)-int(bird.position.x),dy=int(point.y)-int(bird.position.y);
    const int squared=dx*dx+dy*dy;
    const bool sameRoom=area.definition.activation.room(point)==area.definition.activation.room(bird.position);
    if(target && !sameRoom && squared>144) {
        if(phase<1) {radius(point,9,0);idle(1);return StepStatus::Complete;}
        const auto landed=land(bird.position);
        if(!landed) return isDeferred(landed.status)?StepStatus::Blocked:StepStatus::Complete;
        if(*landed.value) return StepStatus::Complete;
    }
    if(!phase && !ports_.monsters.minionOwner(id) && target && squared>144 && chance(60)) {
        controller.phase=24+int(limitedRandom(controller.random,8));
        const auto shown=pose(8,true);if(!shown) return isDeferred(shown.status)?StepStatus::Blocked:StepStatus::Complete;
        radius(point,8,8);idle(12);return StepStatus::Complete;
    }
    bool wounded=false;
    const auto nearby=[&](RegionId region,Vec position) {
        if(!ports_.areas.nearby(bird.area,bird.position,region,position)) return false;
        const auto global=position+ports_.areas.at(region).definition.origin-area.definition.origin;
        const int x=int(global.x)-int(bird.position.x),y=int(global.y)-int(bird.position.y);
        return x*x+y*y<=121;
    };
    for(const auto &[key,other]:ports_.monsters.read().actors)
        if(key!=id && other.alive() && nearby(other.area,other.position) && other.life<=int64_t(p[2])*other.maximumLife/100) wounded=true;
    for(const auto &[key,player]:ports_.players.all()) {
        (void)key;
        if(player.entered && player.persistent.player.hp>0 && nearby(player.area,player.position) &&
           player.persistent.player.hp.raw()<=int64_t(p[2])*player.totals.character.maxLifeFixed.raw()/100) wounded=true;
    }
    const int distance=target?monsterAiDistance(point,target->type?size:0,bird.position):INT32_MAX;
    if(phase>1 && !wounded && (distance>=6 || !chance(15))) {
        const auto shown=pose(8,true);if(!shown) return isDeferred(shown.status)?StepStatus::Blocked:StepStatus::Complete;
        // SetVelocity's argument 1 is translated to path 7 by the tactic;
        // both a new destination and a retained one therefore use path 7.
        Vec goal;const uint8_t path=7;
        if(controller.flightGoal && monsterAiDistance(*controller.flightGoal,0,bird.position)>1) {
            goal=*controller.flightGoal;
            const auto global=goal+area.definition.origin;
            const auto clearFrom=[&](Vec anchor) {
                const auto anchorPoint=anchor+area.definition.origin;
                const auto room=ports_.areas.roomAtPosition(bird.area,anchor,int(anchorPoint.x),int(anchorPoint.y));
                return room?ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),bird.rule.size,0xffff,
                    ports_.spatial.nativeCollisionSample(),{},0,true):std::optional<RoomPoint>{};
            };
            auto free=target?clearFrom(point):std::optional<RoomPoint>{};
            if(!free) free=clearFrom(bird.position);
            if(free) goal=free->position+ports_.areas.at(free->room.area).definition.origin-area.definition.origin;
            controller.flightGoal=goal;
        } else {
            const int radius=phase+8;
            goal={float(int(point.x)+int(limitedRandom(controller.random,uint32_t(2*radius)))-radius),
                  float(int(point.y)+int(limitedRandom(controller.random,uint32_t(2*radius)))-radius)};
            const int minimum=std::clamp(2*radius,12,36);
            while(monsterAiDistance(goal,0,bird.position)<minimum) {
                const int x=(goal.x>int(bird.position.x))-(goal.x<int(bird.position.x));
                const int y=(goal.y>int(bird.position.y))-(goal.y<int(bird.position.y));
                goal.x+=float(x);goal.y+=float(y);
                if(!x && !y) {goal.x+=1;goal.y+=1;}
            }
            controller.flightGoal=goal;
        }
        move(goal+Vec{.5f,.5f},path);--controller.phase;idle(12);return StepStatus::Complete;
    }
    int localPhase=phase;
    if(phase>=1) {
        localPhase=1;controller.phase=1;
        const auto landed=land(point);
        if(!landed) return isDeferred(landed.status)?StepStatus::Blocked:StepStatus::Complete;
        if(*landed.value) return StepStatus::Complete;
        controller.phase=8;idle(12);
    }
    const bool contact=target && meleeDistance(bird.position,bird.rule.size,point,size)<=bird.rule.meleeRange &&
        area.definition.collision.missileSegment(bird.position,point,{4,1});
    if(!contact) {
        if(localPhase!=-1 && !chance(p[4])) {idle(p[1]);return StepStatus::Complete;}
        if(chance(p[3])) {
            const int turn=(rollRandom(controller.random)&255)<128?1:-1;
            controller.pursuing=bool(ports_.monsters.requestMove({id,{bird.area,area.generation,point},target?target->id:EntityId{},0,75,false,0,turn,6}));
        } else radius(point,9,0);
        controller.phase=0;idle(12);return StepStatus::Complete;
    }
    if(chance(p[0])) {
        const auto attack=requestAction(controller,{id,0,*target,tick.tick,4});
        if(isDeferred(attack.status)) return StepStatus::Blocked;
        if(!attack) idle(10);
    } else idle(p[1]);
    return StepStatus::Complete;
}
}
