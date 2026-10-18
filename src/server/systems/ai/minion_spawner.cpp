#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/alignment.hpp"
#include "gameplay/rules/arithmetic.hpp"

namespace d2x::server::ai {
StepStatus System::minionSpawnerFamily(EntityId id,std::optional<UnitTarget> target,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    if(controller.loop>=params[0]) {controller.nextDecision=UINT64_MAX;return StepStatus::Complete;}
    const auto idle=[&]{ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[1]);};
    if(distance>params[3] || nativeNarrow(uint32_t(tick.tick))<controller.phase) {idle();return StepStatus::Complete;}
    int count{};
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate))
                if(candidate!=id && other->alive() && other->alignment()==UnitAlignment::Evil &&
                   (other->rule.ai.kind==MonsterAiKind::Minion || other->rule.ai.kind==MonsterAiKind::SuicideMinion)) ++count;
    const auto skill=unit.rule.skillIds[0];const auto action=unit.rule.skillActions.find(skill);
    // Fn121 compares the population against aip2 (also its idle duration).
    // The apparent maximum-in-area aip5 is unused by the retail callback.
    if(action==unit.rule.skillActions.end() || count>=params[1]) {idle();return StepStatus::Complete;}
    controller.phase=nativeAdd(nativeNarrow(uint32_t(tick.tick)),params[2]);++controller.loop;
    const auto result=requestAction(controller,{id,skill,target.value_or(UnitTarget{}),tick.tick,action->second.nativeMode});
    controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
