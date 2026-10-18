#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <array>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::hydraFamily(EntityId id,std::optional<UnitTarget> target,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    if(nativeNarrow(uint32_t(unit.hydraExpires))<nativeNarrow(uint32_t(tick.tick))) {
        const auto result=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    if(target && distance<25 && limitedRandom(controller.random,100)<60) {
        const auto result=requestAction(controller,{id,unit.rule.skillIds[0],*target,tick.tick,4});
        controller.nextDecision=tick.tick+(result?1:10);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;return StepStatus::Complete;
}
StepStatus System::highPriestFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;auto &random=controller.random;
    const auto chance=[&](int percent){return int(limitedRandom(random,100))<percent;};
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto cast=[&](int slot,uint8_t mode,UnitTarget victim,std::optional<Vec> at={}) {
        const auto skill=slot<0?uint16_t(0):unit.rule.skillIds[size_t(slot)];
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode,at});
        controller.nextDecision=tick.tick+(result?1:10);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto move=[&](Vec goal,EntityId victim,int steps=1,int pathDistance=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,goal},victim,0,75,false,steps,0,0,13,uint8_t(pathDistance)});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    const auto escape=[&] {if(target) move(monsterRetreatPoint(unit.position,point,6),{},1,6);else controller.nextDecision=tick.tick+10;};
    const auto wander=[&](int radius){move(monsterWanderPoint(unit.position,radius,random),{});};
    const auto circle=[&] {
        const int turn=(rollRandom(random)&255)<128?1:-1;
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1,turn,3});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);
    };
    const auto idle=[&] {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+10;};
    const auto primary=target.value_or(UnitTarget{});
    if(!controller.phase) {
        if(contact) {
            if(!chance(params[0])) {escape();return StepStatus::Complete;}
            controller.phase=1;return cast(-1,4,primary);
        }
        if(unit.rule.skillIds[1] && now>controller.loop && chance(params[1])) {
            std::optional<UnitTarget> heal;int minimumLife=75;
            if(const auto *room=area.definition.activation.room(unit.position))
                for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
                    for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                        if(!other->alive() || !sameAlignment(unit.alignment(),other->alignment()) || other->alignment()!=UnitAlignment::Evil) continue;
                        const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                        const int x=int(std::floor(at.x))-int(std::floor(unit.position.x)),y=int(std::floor(at.y))-int(std::floor(unit.position.y));
                        if(x*x+y*y>2500) continue;
                        const int maximum=int(other->maximumLife>>8);
                        const int life=maximum?nativeMultiply(100,int(other->life>>8))/maximum:0;
                        if(life>75 || life>=minimumLife) continue;
                        minimumLife=life;heal=UnitTarget{candidate,0,1};
                    }
            if(heal) {controller.loop=nativeAdd(now,params[2]);return cast(1,unit.rule.skillActions.at(unit.rule.skillIds[1]).nativeMode,*heal);}
        }
        if(unit.rule.skillIds[0] && now>controller.loop && distance<params[7] && chance(params[3])) {
            constexpr std::array<Vec,4> offsets{{{-5,-5},{5,-5},{5,5},{-5,5}}};
            const auto at=point+offsets[rollRandom(random)&3];
            // ModeChange keeps pTargetUnit when both unit and coordinates
            // are supplied. St14/Do144 resolve that live unit first; the
            // random corner is consumed but does not replace its position.
            const auto result=cast(0,unit.rule.skillActions.at(unit.rule.skillIds[0]).nativeMode,primary,target?std::nullopt:std::optional{at});
            controller.loop=nativeAdd(now,100);return result;
        }
        const auto lightning=unit.rule.attacks.find(8);
        if(params[4]>0 && lightning!=unit.rule.attacks.end() && lightning->second.missile &&
            distance<lightning->second.missile->frames-2 && chance(params[4])) return cast(-1,8,primary);
        if(chance(80)) {if(distance<=params[7]) circle();else move(point,primary.id,6);return StepStatus::Complete;}
    }
    controller.phase=1;
    if(contact) {
        if(chance(params[6])) return cast(-1,8,primary);
        if(chance(params[5])) {escape();controller.phase=0;return StepStatus::Complete;}
        if(chance(90)) return cast(-1,4,primary);
        idle();return StepStatus::Complete;
    }
    if(distance<6 && chance(params[6])) return cast(-1,8,primary);
    if(chance(params[5])) {controller.phase=0;idle();return StepStatus::Complete;}
    if(!chance(70)) wander(12);
    else if(!move(point,primary.id)) {if(limitedRandom(random,100)>=70) controller.nextDecision=tick.tick+10;else wander(4);}
    return StepStatus::Complete;
}
}
