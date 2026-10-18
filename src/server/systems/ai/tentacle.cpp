#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::tentacleFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &params=unit.rule.ai.params;
    const bool head=unit.rule.ai.kind==MonsterAiKind::TentacleHead;
    controller.pursuing=false;
    const auto chance=[&](int percent) {return int(limitedRandom(controller.random,100))<percent;};
    const auto kill=[&](EntityId source) {
        const auto result=perform(controller,[this,id,source,tick]{return ports_.monsters.scriptDeath(id,tick.tick,source);});
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto *owner=head?nullptr:ports_.monsters.find(ports_.monsters.minionOwner(id));
    if(!head) {
        if(!owner) return kill({});
        if(ports_.monsters.deadMode(owner->id,tick.tick) && chance(40)) return kill(owner->movementTarget);
    }
    const auto &area=ports_.areas.at(unit.area).definition;
    const bool combat=target && unitMeleeReach(area.collision,unit.position,unit.rule.size,
        point,size,unit.rule.meleeRange);
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto idle=[&](int frames,bool neutral=false) {
        if(neutral) {ports_.skills.cancel({},id);ports_.monsters.neutralMode(id);}
        controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));
    };
    const auto cast=[&](size_t slot,UnitTarget victim) {
        const auto skill=uint16_t(unit.rule.skillIds.at(slot));
        return requestAction(controller,{id,skill,victim,tick.tick,unit.rule.skillActions.at(skill).nativeMode});
    };
    const auto hasSkill=[&](size_t slot) {return unit.rule.skillIds.at(slot)!=0;};
    const auto submerge=[&](int wait) {
        const auto result=cast(0,target.value_or(UnitTarget{}));
        if(isDeferred(result.status)) return StepStatus::Blocked;
        idle(wait);controller.loop=nativeAdd(now,nativeMultiply(25,params[2]));controller.thirdParameter=1;
        return StepStatus::Complete;
    };
    if(hasSkill(0)) {
        if(!controller.thirdParameter) return submerge(8);
        if(controller.thirdParameter==2 && now>controller.loop &&
           (distance>params[5] || (!combat && chance(params[1])) || (!head && owner->nativeMode==14 && chance(50))))
            return submerge(head?20:8);
    }
    if(hasSkill(1) && controller.thirdParameter==1 && now>controller.loop &&
       (combat || distance<params[5] || (!head && owner->nativeMode!=14 && chance(5)))) {
        const auto result=cast(1,{id,0,1});
        if(isDeferred(result.status)) return StepStatus::Blocked;
        controller.loop=nativeAdd(now,nativeMultiply(25,params[3]));controller.thirdParameter=2;
        return StepStatus::Complete;
    }
    if(controller.thirdParameter==1) {idle(params[4]);return StepStatus::Complete;}
    if(head) {
        // Fn057 reacquires through AiUtil's shooting callback before rolling,
        // even when no target passes its missile barrier/range qualification.
        const auto shot=shootableTarget(unit,tick);
        if(chance(params[0])) {
            const auto result=requestAction(controller,{id,0,shot?shot->unit:UnitTarget{},tick.tick,4});
            return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        idle(params[4]);
    } else if(combat && chance(params[0])) {
        const auto result=requestAction(controller,{id,0,*target,tick.tick,4});
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    } else idle(params[4],true);
    return StepStatus::Complete;
}
}
