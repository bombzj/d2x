#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "core/random.hpp"
namespace d2x::server::ai {
StepStatus System::nestFamily(EntityId id,UnitTarget target,int distance,TickContext tick,Controller &controller) {
    const auto &nest=*ports_.monsters.find(id);const auto &p=nest.rule.ai.params;
    // master monster_nest / AITHINK_Fn043. Stationary actors do not borrow a
    // walking or melee clock. Birth uses the prepared MonSeq event 4 clock.
    if(distance>20) {controller.nextDecision=tick.tick+25;return StepStatus::Complete;}
    if(controller.act2Attempts>=p[2]) {
        ports_.monsters.disableTreasure(id);
        const auto result=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
        if(isDeferred(result.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
        return StepStatus::Complete;
    }
    if(tick.tick-controller.lastSpawn<uint64_t(p[0])) {
        controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);return StepStatus::Complete;
    }
    // Fn043 consumes its interval even when the birth preflight fails;
    // successful cast attempts, rather than surviving children, exhaust it.
    controller.lastSpawn=tick.tick;
    if(!ports_.monsters.canSpawnNestChild(id,ports_.spatial.nativeCollisionSample())) {
        controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);
        return StepStatus::Complete;
    }
    ++controller.act2Attempts;
    const auto skill=nest.rule.skillIds[0];
    const auto result=requestAction(controller,{id,skill,target,tick.tick,nest.rule.skillActions.at(skill).nativeMode});
    if(isDeferred(result.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    if(!result) controller.nextDecision=tick.tick+10;
    return StepStatus::Complete;
}
}
