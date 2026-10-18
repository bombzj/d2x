#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::genericSpawnerFamily(EntityId id,std::optional<UnitTarget> target,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area).definition;const auto &params=unit.rule.ai.params;
    const auto idle=[&]{ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+20;};
    if(!target) {idle();return StepStatus::Complete;}
    if(unit.minionSpawnClass<0 || unit.minionSpawnClassAiRevision!=unit.aiFunctionRevision) {
        const auto &roster=area.genericSpawnRoster;int selected=area.genericSpawnFallback;
        if(!roster.empty()) {
            const auto &chosen=roster[size_t(limitedRandom(controller.random,uint32_t(roster.size())))];
            if(chosen.second) selected=chosen.first;
            else if(const auto first=std::find_if(roster.begin(),roster.end(),[](const auto &entry){return entry.second;});first!=roster.end()) selected=first->first;
        }
        const auto saved=perform(controller,[this,id,selected]{return ports_.monsters.setMinionSpawnClass(id,selected);});
        if(isDeferred(saved.status)) return StepStatus::Blocked;
    }
    if(distance>20) {idle();return StepStatus::Complete;}
    if(controller.loop>=params[2]) {
        ports_.monsters.disableTreasure(id);
        const auto killed=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
        return isDeferred(killed.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const int32_t now=nativeNarrow(uint32_t(tick.tick)),elapsed=nativeSubtract(now,controller.phase);
    const int32_t magnitude=elapsed<0?nativeSubtract(0,elapsed):elapsed;
    if(magnitude<params[0]) {idle();return StepStatus::Complete;}
    controller.phase=now; // Failed preflight still consumes this interval.
    if(!ports_.monsters.canSpawnNestChild(id,ports_.spatial.nativeCollisionSample())) {idle();return StepStatus::Complete;}
    ++controller.loop; // Counts accepted preflights, independently of child survival.
    const auto skill=unit.rule.skillIds[0];
    const auto cast=requestAction(controller,{id,skill,*target,tick.tick,unit.rule.skillActions.at(skill).nativeMode});
    controller.nextDecision=tick.tick+(cast?1:10);return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
