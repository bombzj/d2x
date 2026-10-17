#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::frogFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &frog=*ports_.monsters.find(id);const auto &p=frog.rule.ai.params;
    const auto &area=ports_.areas.at(frog.area);
    const int distance=target?monsterAiDistance(point,target->type?size:0,frog.position):INT32_MAX;
    const auto idle=[&](int frames,bool neutral=false) {
        if(neutral) {
            ports_.skills.cancel({},id);
            ports_.monsters.aiPose(id,1,false,false,tick.tick,frames);
        }
        controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));
    };
    const auto skill=[&](size_t slot,UnitTarget victim) {
        const auto skill=uint16_t(frog.rule.skillIds.at(slot));
        return requestAction(controller,{id,skill,victim,tick.tick,frog.rule.skillActions.at(skill).nativeMode});
    };
    const auto emerge=[&]()->DomainResult<bool> {
        const auto restored=ports_.monsters.restoreGroundPresence(id,ports_.spatial.nativeCollisionSample());
        if(!restored || !*restored.value) return restored;
        const auto started=skill(1,{id,0,1});
        if(isDeferred(started.status)) return {started.status,{}};
        controller.phase=2;return {DomainStatus::Applied,true};
    };
    if(!controller.phase) {
        if(distance>12) {
            const auto started=skill(0,target.value_or(UnitTarget{id,0,1}));
            if(isDeferred(started.status)) return StepStatus::Blocked;
            controller.phase=1;ports_.monsters.submerge(id);idle(12);return StepStatus::Complete;
        }
        const auto emerged=emerge();
        if(!emerged) return isDeferred(emerged.status)?StepStatus::Blocked:StepStatus::Complete;
        if(*emerged.value) return StepStatus::Complete;
        const auto global=frog.position+ports_.areas.at(frog.area).definition.origin;
        const auto room=ports_.areas.roomAtPosition(frog.area,frog.position,int(global.x),int(global.y));
        if(room && !ports_.areas.nativeCollisionMask(*room,int(global.x),int(global.y),1,0xc01,ports_.spatial.nativeCollisionSample())) controller.phase=2;
        idle(12,true);return StepStatus::Complete;
    }
    if(controller.phase==1) {
        if(distance<p[7]) {
            const auto emerged=emerge();
            if(!emerged) return isDeferred(emerged.status)?StepStatus::Blocked:StepStatus::Complete;
            if(*emerged.value) return StepStatus::Complete;
        }
        if(distance<20 && controller.loop>64) {
            const auto emerged=emerge();
            if(!emerged) return isDeferred(emerged.status)?StepStatus::Blocked:StepStatus::Complete;
            if(*emerged.value) return StepStatus::Complete;
        }
        ports_.monsters.submerge(id);++controller.loop;idle(24);return StepStatus::Complete;
    }
    if(!target) {idle(32);return StepStatus::Complete;}
    const auto chance=[&](int percent) {return int(limitedRandom(controller.random,100))<percent;};
    const auto circle=[&](int radius) {
        const int turn=(rollRandom(controller.random)&255)<128?1:-1;
        controller.pursuing=bool(ports_.monsters.requestMove({id,{frog.area,area.generation,point},target->id,0,75,false,0,turn,radius}));
        idle(controller.pursuing?1:10);
    };
    const auto attack=[&](uint8_t mode) {
        const auto result=requestAction(controller,{id,0,*target,tick.tick,mode});
        if(!result) idle(10);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=meleeDistance(frog.position,frog.rule.size,point,size)<=frog.rule.meleeRange &&
        area.definition.collision.missileSegment(frog.position,point,{4,1});
    if(contact) {
        if(chance(p[1])) return attack(5);
        if(chance(p[0])) return attack(4);
        if(chance(p[2])) circle(3);else idle(p[6],true);
    } else if(distance>=p[5]) {
        if(chance(p[3])) circle(3);
        else {
            controller.pursuing=bool(ports_.monsters.requestMove({id,{frog.area,area.generation,point},target->id,0,75,false,4}));
            idle(controller.pursuing?1:10);
        }
    } else if(chance(p[4])) return attack(5);
    else if(chance(p[3])) circle(4);
    else idle(p[6],true);
    return StepStatus::Complete;
}
}
