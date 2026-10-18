#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::frozenHorrorFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto stats=ports_.effects.unitStatValues(id,tick.tick);if(!stats) return StepStatus::Blocked;
    const auto ranks=nativeMonsterSkillRanks(unit.rule.baseSkillRanks,*stats,*unit.rule.stats,*unit.rule.formulas);
    const auto skill=unit.rule.skillIds[0];const auto learned=ranks.find(skill);
    const int rank=learned==ranks.end()?0:std::max(learned->second,0);
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const bool inferno=ports_.effects.unitStates(id,tick.tick).contains(unit.rule.formulas->infernoState);
    if(skill && distance<rank && chance(params[2]) && !inferno) {
        const auto result=requestAction(controller,{id,skill,target.value_or(UnitTarget{}),tick.tick,unit.rule.skillActions.at(skill).nativeMode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    const bool attack=contact && chance(params[0]);
    const bool walk=!contact && chance(params[1]);
    if(inferno) {
        const auto cleared=perform(controller,[this,id,state=unit.rule.formulas->infernoState,tick]{return ports_.monsters.toggleAiState(id,state,false,tick.tick);});
        if(isDeferred(cleared.status)) return StepStatus::Blocked;
    }
    if(attack) {
        const auto result=requestAction(controller,{id,0,target.value_or(UnitTarget{}),tick.tick,4});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto result=perform(controller,[this,id,walk,point,target,tick,wait=params[3]]()->DomainResult<> {
        auto &controller=state_.controllers.at(id);const auto *unit=ports_.monsters.find(id);
        if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        if(walk) {
            const auto &area=ports_.areas.at(unit->area);
            const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,point},target?target->id:EntityId{},0,75,false,1});
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
        } else {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(wait);}
        return {DomainStatus::Applied,std::monostate{}};
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
