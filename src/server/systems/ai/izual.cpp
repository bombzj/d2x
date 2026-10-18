#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::izualFamily(EntityId id,UnitTarget target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const bool uber=unit.rule.ai.kind==MonsterAiKind::UberIzual;
    if(!uber && !controller.phase) {
        const auto activated=ports_.quests.activateIzual(tick.tick);
        if(isDeferred(activated.status)) return StepStatus::Blocked;
        controller.phase=1;
    }
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int delay){ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(delay);};
    const auto cast=[&](uint16_t skill,uint8_t mode) {
        const auto result=requestAction(controller,{id,skill,target,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(uber) {
        const auto selfCast=[&](size_t slot,std::optional<Vec> at={}) {
            const auto skill=unit.rule.skillIds[slot];
            const auto result=requestAction(controller,{id,skill,UnitTarget{},tick.tick,unit.rule.skillActions.at(skill).nativeMode,at});
            controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        };
        const auto armor=unit.rule.skillIds[1];
        if(armor && unit.rule.skillActions.contains(armor)) {
            const int state=unit.rule.formulas->skills.at(armor).auraState;
            if(state>=0 && !ports_.effects.unitStates(id,tick.tick).contains(state)) return selfCast(1);
        }
        if(!area.definition.collision.missileSegment(unit.position,point,{6,1})) return selfCast(2,point);
    }
    if(controller.loop) {idle(controller.loop);controller.loop=0;return StepStatus::Complete;}
    const auto nova=unit.rule.skillIds[0];
    const bool hasNova=nova && unit.rule.skillActions.contains(nova);
    const auto frostNova=[&] {
        controller.loop=params[4];controller.thirdParameter=params[5];
        return cast(nova,unit.rule.skillActions.at(nova).nativeMode);
    };
    const auto walk=[&] {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target.id,0,75,false,1});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
        if(!moved) {
            // WalkToTargetUnitWithFlags(7)'s native failure continuation.
            if(chance(70)) {
                const auto at=monsterWanderPoint(unit.position,4,controller.random);
                const auto fallback=ports_.monsters.requestMove({id,{unit.area,area.generation,at},{},0,75,false,1});
                controller.pursuing=bool(fallback);
            } else idle(10);
        }
    };
    const bool contact=meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(!contact) {
        if(hasNova && distance<10) {
            if(controller.thirdParameter>0) {walk();return StepStatus::Complete;}
            if(chance(params[2])) return frostNova();
        }
        if(controller.thirdParameter<=0 && !chance(params[1])) {
            if(distance<=10) idle(unit.rule.neutralDelayTicks);
            else {
                const auto at=monsterRadiusApproachPoint(unit.position,unit.rule.size,point,6,9);
                const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,at},{},0,75,false,1});
                controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
            }
            return StepStatus::Complete;
        }
        walk();return StepStatus::Complete;
    }
    if(controller.thirdParameter<=0 && !chance(params[0])) {
        controller.thirdParameter=0;
        if(hasNova && chance(params[3])) return frostNova();
        idle(unit.rule.neutralDelayTicks);return StepStatus::Complete;
    }
    if(controller.thirdParameter>0) --controller.thirdParameter;
    return cast(0,4);
}
}
