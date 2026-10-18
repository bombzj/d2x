#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::impFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &rule=unit.rule;
    const auto &first=rule.impParameters[0],&second=rule.impParameters[1],&third=rule.impParameters[2],&fourth=rule.impParameters[3];
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto cast=[&](uint16_t skill,uint8_t mode,UnitTarget victim,std::optional<Vec> at=std::nullopt) {
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode,at});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto teleport=[&](bool base) {
        const auto draw=[&]{return int(limitedRandom(controller.random,uint32_t(2*first[1])))-first[1];};
        const Vec at{float(std::floor(unit.position.x)+draw()),float(std::floor(unit.position.y)+draw())};
        const auto skill=base?rule.impBaseSkills[0]:rule.skillIds[0];
        return cast(skill,base?rule.impBaseModes[0]:rule.skillActions.at(skill).nativeMode,{},at);
    };
    const auto move=[&](Vec at,EntityId victim,uint8_t steps=1,uint8_t path=13) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,75,false,steps,0,0,path,5});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    if(unit.impMountedAi) {
        const auto *owner=ports_.monsters.find(unit.nativeOwner);
        if(!unit.aiStates.contains(rule.attachedState) || !owner) {ports_.monsters.resetImpAi(id);idle(1);return StepStatus::Complete;}
        if(distance>24 || !owner->alive() || owner->alignment()!=UnitAlignment::Evil || !unit.impMountedSkill) {
            controller.carrier.reset();return teleport(true);
        }
        if(distance>=second[1] || !chance(second[2])) {
            if(chance(50)) {
                const auto result=perform(controller,[this,id,tick]{return ports_.monsters.aiPose(id,8,false,false,tick.tick);});
                controller.nextDecision=tick.tick+1;return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
            }
        } else {
            if(unit.impMountedParameter && second[3]>0) {rollRandom(controller.random);rollRandom(controller.random);}
            const auto slot=unit.impMountedSkill;
            if(slot<rule.impBaseSkills.size() && rule.impBaseSkills[slot])
                return cast(rule.impBaseSkills[slot],rule.impBaseModes[slot],target.value_or(UnitTarget{}));
        }
        idle(20);return StepStatus::Complete;
    }
    if(controller.carrier && unit.alignment()==UnitAlignment::Evil) {
        const auto *carrier=ports_.monsters.find(*controller.carrier);
        if(!carrier || !carrier->alive() || ports_.monsters.find(carrier->nativeOwner) || carrier->alignment()!=UnitAlignment::Evil) controller.carrier.reset();
        else {
            const auto at=carrier->position+ports_.areas.at(carrier->area).definition.origin-area.definition.origin;
            const int dx=int(std::floor(at.x))-int(std::floor(unit.position.x)),dy=int(std::floor(at.y))-int(std::floor(unit.position.y));
            if(dx*dx+dy*dy>second[0]*second[0]) {move(at,carrier->id);return StepStatus::Complete;}
            if(rule.skillIds[0]) return cast(rule.skillIds[0],rule.skillActions.at(rule.skillIds[0]).nativeMode,{carrier->id,0,1});
        }
    }
    const bool contact=target && meleeDistance(unit.position,rule.size,point,size)<=rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact) {
        const int64_t maximum=unit.maximumLife>>8,life=unit.life>>8;
        if(rule.skillIds[0] && (maximum?100*life/maximum:0)<first[0]) return teleport(false);
        if(chance(third[1]) && move(monsterRetreatPoint(unit.position,point,5),{},1,2)) return StepStatus::Complete;
    }
    if(rule.skillIds[0] && chance(first[2])) return teleport(false);
    if(distance<third[0] && chance(third[1])) {
        move(monsterRetreatPoint(unit.position,point,5),{});return StepStatus::Complete;
    }
    if(rule.skillIds[3]) if(const auto shootable=shootableTarget(unit,tick)) {
        const bool fire=shootable->distance<third[2]?chance(third[3]):shootable->distance<fourth[2] && chance(fourth[3]);
        if(fire) return cast(rule.skillIds[3],rule.skillActions.at(rule.skillIds[3]).nativeMode,shootable->unit);
    }
    if(chance(33)) {move(point,target?target->id:EntityId{},4);return StepStatus::Complete;}
    if(chance(20)) move(monsterWanderPoint(unit.position,8,controller.random),{});else idle(10);
    return StepStatus::Complete;
}
}
