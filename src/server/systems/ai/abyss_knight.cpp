#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::abyssKnightFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto cast=[&](int slot,UnitTarget victim) {
        const auto skill=slot<0?uint16_t(0):unit.rule.skillIds[size_t(slot)];
        const auto mode=slot<0?uint8_t(4):unit.rule.skillActions.at(skill).nativeMode;
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto armor=unit.rule.skillIds[1];
    const int maximum=int(unit.maximumLife>>8),life=maximum?nativeMultiply(100,int(unit.life>>8))/maximum:0;
    if(armor && unit.rule.boneArmorState>=0 && !ports_.effects.unitStates(id,tick.tick).contains(unit.rule.boneArmorState) && life<params[0] && chance(params[1]))
        return cast(1,{id,0,1});
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact) {if(chance(params[2])) return cast(-1,*target);idle(params[3]);return StepStatus::Complete;}
    if(distance<params[4] && controller.phase<=0) controller.phase=params[5];
    if(unit.rule.skillIds[0] && !controller.phase && unit.components[10]<4) {
        const auto result=cast(0,target.value_or(UnitTarget{}));controller.phase=params[5];return result;
    }
    if(controller.phase>0) --controller.phase;
    if(chance(params[6])) {
        const auto pathDistance=uint8_t((rollRandom(controller.random)&1)+6);
        auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1,0,0,2,pathDistance});
        if(!moved) {
            if(limitedRandom(controller.random,100)>=70) {idle(10);return StepStatus::Complete;}
            const auto at=monsterWanderPoint(unit.position,4,controller.random);
            moved=ports_.monsters.requestMove({id,{unit.area,area.generation,at},{},0,75,false,1});
        }
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
    }
    if(distance<params[7]) {
        const int turn=(rollRandom(controller.random)&255)<128?1:-1;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1,turn,3});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else idle(15);
    return StepStatus::Complete;
}
}
