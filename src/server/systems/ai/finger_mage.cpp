#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::fingerMageFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto move=[&](Vec at,EntityId victim,int velocity=75,int pathDistance=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,velocity,false,1,0,0,13,uint8_t(pathDistance)});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    const auto walk=[&](bool fallback) {
        if(!move(point,target?target->id:EntityId{}) && fallback) {
            if(limitedRandom(controller.random,100)>=70) idle(10);
            else move(monsterWanderPoint(unit.position,4,controller.random),{});
        }
    };
    const auto escape=[&](int radius,int velocity) {if(target) move(monsterRetreatPoint(unit.position,point,radius),{},velocity,radius);else controller.nextDecision=tick.tick+10;};
    const auto cast=[&](bool skill) {
        const auto chosen=skill?unit.rule.skillIds[0]:uint16_t(0);
        const auto mode=skill?unit.rule.skillActions.at(chosen).nativeMode:uint8_t(4);
        const auto result=requestAction(controller,{id,chosen,target.value_or(UnitTarget{}),tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    const bool retaliating=unit.hitOccurrence!=controller.observedHit;controller.observedHit=unit.hitOccurrence;
    const bool hasSkill=unit.rule.skillIds[0] && unit.rule.skillActions.contains(unit.rule.skillIds[0]);
    const int maximum=int(unit.maximumLife>>8),life=maximum?nativeMultiply(100,int(unit.life>>8))/maximum:0;
    if(retaliating) {
        controller.phase=0;
        if(contact) return cast(false);
        if(hasSkill && distance<params[4]) return cast(true);
    }
    if(controller.phase==1) {
        ++controller.loop;
        if(life>params[2] || chance(25) || controller.loop>params[5]) controller.phase=0;
        else if(distance<14) {escape(14,75+unit.rule.ai.retreatVelocityBonus);return StepStatus::Complete;}
        idle(15);return StepStatus::Complete;
    }
    const auto owner=ports_.monsters.minionOwner(id);
    if((!owner || (!ports_.monsters.find(owner) && !ports_.players.findActor(owner))) && life<params[3]) {
        controller.phase=1;controller.loop=0;escape(9,75);return StepStatus::Complete;
    }
    if(contact) {
        if(chance(params[0])) return cast(false);
        if(hasSkill && chance(params[1])) return cast(true);
        const int turn=(rollRandom(controller.random)&255)<128?1:-1;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target->id,0,75,false,1,turn,5});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
    }
    if(distance>=params[4]) {
        if(distance>=params[6]) idle(15);else walk(true);
    } else {
        if(hasSkill && chance(params[1])) return cast(true);
        if(target && ports_.effects.unitStates(target->id,tick.tick).contains(unit.rule.fingerMageCurseState)) walk(false);else idle(params[7]);
    }
    return StepStatus::Complete;
}
}
