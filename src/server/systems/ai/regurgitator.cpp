#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::regurgitatorFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    if(target && unit.rule.stats && unit.rule.stats->pd2 && (unit.rule.nativeClass==1064 || unit.rule.nativeClass==1065))
        return pd2Guardian(id,*target,tick,controller);
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto position=[&](const monsters::Actor &other){return other.position+ports_.areas.at(other.area).definition.origin-area.definition.origin;};
    const auto squared=[&](Vec at) {
        const int x=int(std::floor(at.x))-int(std::floor(unit.position.x)),y=int(std::floor(at.y))-int(std::floor(unit.position.y));
        return x*x+y*y;
    };
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto move=[&](Vec at,EntityId victim,int radius=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,75,false,1,0,0,13,uint8_t(radius)});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);
    };
    const auto wander=[&] {move(monsterWanderPoint(unit.position,8,controller.random),{});};
    const auto reset=[&] {controller.phase=controller.loop=controller.thirdParameter=0;wander();};
    const auto escape=[&](int radius){if(target) move(monsterRetreatPoint(unit.position,point,radius),{},radius);else controller.nextDecision=tick.tick+10;};
    const auto cast=[&](uint16_t skill,uint8_t mode,UnitTarget victim) {
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(controller.phase==2 || controller.phase==3) {
        const auto *corpse=ports_.monsters.find(EntityId{uint32_t(controller.loop)});
        if(!corpse || !ports_.monsters.deadMode(corpse->id,tick.tick)) {reset();return StepStatus::Complete;}
        if(controller.phase==2) {
            if(squared(position(*corpse))>4) {
                if(controller.thirdParameter>=6) {reset();return StepStatus::Complete;}
                move(position(*corpse),corpse->id);++controller.thirdParameter;
            }
            // Retail enters NU after the optional walk in the same callback.
            controller.phase=3;idle(8);return StepStatus::Complete;
        }
        const auto skill=unit.rule.skillIds[0];
        if(!skill) {reset();return StepStatus::Complete;}
        const auto result=cast(skill,unit.rule.skillActions.at(skill).nativeMode,{corpse->id,0,1});
        controller.phase=4;return result;
    }
    if(controller.phase==4) {
        if(contact) {escape(8);return StepStatus::Complete;}
        const auto result=cast(0,5,target.value_or(UnitTarget{}));controller.phase=5;return result;
    }
    if(controller.phase==5) {escape(16);controller.phase=0;return StepStatus::Complete;}
    const monsters::Actor *food=nullptr;int minimum=params[5]*params[5];
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *corpse=ports_.monsters.find(candidate)) {
                if(candidate==id || corpse->alive() || corpse->hireling || corpse->targetFlagsDisabled || corpse->corpse.unavailable ||
                   corpse->alignment()!=UnitAlignment::Evil || !corpse->rule.softCorpse) continue;
                bool unavailable=false;
                for(const auto state:ports_.effects.unitStates(candidate,tick.tick)) {
                    const auto &definition=corpse->rule.states->at(state);
                    unavailable|=definition.corpseUnselectable || definition.hideOnDeath;
                }
                if(unavailable) continue;
                const int distance=squared(position(*corpse));
                if(distance>minimum) continue;
                minimum=distance;food=corpse; // Equal distances replace, preserving the room chain.
            }
    const auto seek=[&] {
        move(position(*food),food->id);controller.phase=2;controller.loop=nativeNarrow(uint32_t(food->id.value));controller.thirdParameter=0;
    };
    if(controller.phase==1) {
        if(!food) {
            if(chance(20)) controller.phase=controller.loop=controller.thirdParameter=0;else wander();
            return StepStatus::Complete;
        }
        controller.loop=nativeNarrow(uint32_t(food->id.value));controller.thirdParameter=0;
        if(squared(position(*food))<=2) {idle(8);controller.phase=3;}else seek();return StepStatus::Complete;
    }
    if(food && ((squared(position(*food))<9 && chance(params[1])) || chance(params[4]))) {seek();return StepStatus::Complete;}
    if(contact) {if(chance(params[0])) return cast(0,4,*target);idle(15);return StepStatus::Complete;}
    if(chance(params[2])) {move(point,target?target->id:EntityId{});return StepStatus::Complete;}
    if(!food || !chance(params[3])) idle(8);else seek();
    return StepStatus::Complete;
}
}
