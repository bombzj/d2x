#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::oblivionKnightFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const bool pd2=unit.rule.stats && unit.rule.stats->pd2;
    const auto chance=[&](int percent){
        const auto roll=pd2?pd2UnitEventRandom(controller.random)%100:limitedRandom(controller.random,100);
        return pd2?roll<uint32_t(percent):int(roll)<percent;
    };
    const auto position=[&](const monsters::Actor &other){return other.position+ports_.areas.at(other.area).definition.origin-area.definition.origin;};
    const auto squared=[](Vec from,Vec to) {
        const int x=int(std::floor(from.x))-int(std::floor(to.x)),y=int(std::floor(from.y))-int(std::floor(to.y));return x*x+y*y;
    };
    const auto hasSkill=[&](size_t slot){return unit.rule.skillIds[slot] && unit.rule.skillActions.contains(unit.rule.skillIds[slot]);};
    const auto cast=[&](size_t slot,UnitTarget victim) {
        const auto skill=unit.rule.skillIds[slot];const auto result=requestAction(controller,{id,skill,victim,tick.tick,unit.rule.skillActions.at(skill).nativeMode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto move=[&](Vec at,EntityId victim,int velocity=75,uint8_t path=13,int range=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,velocity,false,1,0,0,path,uint8_t(range)});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    const monsters::Actor *ally=nullptr;int nearest=INT32_MAX;
    if(const auto *room=area.definition.activation.room(unit.position);!pd2 && room)
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                if(candidate==id || !other->alive() || other->targetFlagsDisabled || other->rule.baseClass!=unit.rule.doomKnightBaseClass ||
                   !sameAlignment(unit.alignment(),other->alignment()) || other->alignment()!=UnitAlignment::Evil) continue;
                const int range=squared(unit.position,position(*other));
                if(range>2500 || range>=nearest) continue;
                nearest=range;ally=other;
            }
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto primary=target.value_or(UnitTarget{});
    if(distance<params[0]) {
        const auto states=ports_.effects.unitStates(primary.id,tick.tick);
        const bool curse=pd2?!states.contains(9) && !states.contains(19) && !states.contains(61) && !states.contains(60):
            unit.rule.closeCurseState>0 && !states.contains(unit.rule.closeCurseState);
        if(hasSkill(3) && curse && (!pd2 || chance(params[3]))) {
            const auto result=cast(3,primary);controller.phase=nativeAdd(now,params[2]);return result;
        }
        if(ally && squared(position(*ally),point)>distance*distance && move(position(*ally),ally->id)) return StepStatus::Complete;
        if(target && move(monsterRetreatPoint(unit.position,point,10),{},125,2,10)) return StepStatus::Complete;
        if(hasSkill(2)) return cast(2,primary);
    }
    const auto shootable=shootableTarget(unit,tick);
    if(shootable && shootable->distance<params[1]) {
        if(!pd2 && hasSkill(5) && now>controller.phase && chance(params[3])) {
            const auto result=cast(5,primary);controller.phase=nativeAdd(now,params[2]);return result;
        }
        if(chance(params[4])) {
            if(hasSkill(2) && chance(params[5])) return cast(2,shootable->unit);
            if(hasSkill(0) && unit.components[10]<4) return cast(0,shootable->unit);
        }
    }
    if(distance>params[7] && chance(params[6])) {
        const auto at=monsterWanderPoint(target?point:unit.position,target?6:2,controller.random);
        move(at,{});return StepStatus::Complete;
    }
    if(chance(70)) {
        // Current 35AE60 sets clockwise path 5 directly. The original
        // Fn074 delegate chooses clockwise/counterclockwise with another roll.
        const int turn=pd2?1:(rollRandom(controller.random)&255)<128?1:-1;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},primary.id,0,75,false,1,turn,3});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+10;}
    return StepStatus::Complete;
}
}
