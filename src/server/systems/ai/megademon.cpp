#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::megademonFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto sampled=ports_.effects.unitStatValues(id,tick.tick);if(!sampled) return StepStatus::Blocked;
    const auto ranks=nativeMonsterSkillRanks(unit.rule.baseSkillRanks,*sampled,*unit.rule.stats,*unit.rule.formulas);
    const auto skill=unit.rule.skillIds[0];const auto learned=ranks.find(skill);
    const int rank=learned==ranks.end()?1:std::max(learned->second,1);
    const bool hasSkill=skill && unit.rule.skillActions.contains(skill);
    const bool inferno=ports_.effects.unitStates(id,tick.tick).contains(unit.rule.formulas->infernoState);
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    enum class Action { Idle,Walk,Circle,Melee,Inferno } action=Action::Idle;
    int wait=10;
    if(hasSkill && !contact && distance<rank && !inferno) {
        if(now>controller.phase && chance(params[0])) action=Action::Inferno;
        else if(chance(params[3])) action=Action::Walk;
    } else if(!contact) {
        if(chance(params[3])) action=Action::Walk;
    } else {
        if(hasSkill && now>controller.phase && chance(params[1])) action=Action::Inferno;
        else if(chance(params[2])) action=Action::Melee;
        else if(chance(params[4])) action=Action::Circle;
        else wait=5;
    }
    const int turn=action==Action::Circle?((rollRandom(controller.random)&255)<128?1:-1):0;
    if(action==Action::Inferno) controller.phase=nativeAdd(now,params[5]);
    if(inferno) {
        const auto result=perform(controller,[this,id,state=unit.rule.formulas->infernoState,tick]{return ports_.monsters.toggleAiState(id,state,false,tick.tick);});
        if(isDeferred(result.status)) return StepStatus::Blocked;
    }
    if(action==Action::Melee || action==Action::Inferno) {
        const auto used=action==Action::Inferno?skill:uint16_t(0);
        const auto mode=action==Action::Inferno?unit.rule.skillActions.at(skill).nativeMode:uint8_t(4);
        const auto result=requestAction(controller,{id,used,target.value_or(UnitTarget{}),tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    controller.nextDecision=tick.tick+uint64_t(action==Action::Idle?wait:1);
    const auto result=perform(controller,[this,id,target,point,action,turn,tick]()->DomainResult<> {
        const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        auto &controller=state_.controllers.at(id);const auto &area=ports_.areas.at(unit->area);
        if(action==Action::Idle) {ports_.monsters.stop(id);controller.pursuing=false;return {DomainStatus::Applied,std::monostate{}};}
        auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,point},target?target->id:EntityId{},0,75,false,1,turn,action==Action::Circle?3:0});
        if(!moved && action==Action::Walk) {
            // Flags7 draws after ModeChange consumed its velocity parameters.
            // The earlier state clear may already have published the AI seed.
            const auto expected=ports_.monsters.find(id)->combatRandom;auto random=expected;
            const bool pause=limitedRandom(random,100)>=70;
            const auto at=pause?Vec{}:monsterWanderPoint(unit->position,4,random);
            const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
            if(!pause) moved=ports_.monsters.requestMove({id,{unit->area,area.generation,at},{},0,75,false,1});
        }
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
        return {DomainStatus::Applied,std::monostate{}};
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
