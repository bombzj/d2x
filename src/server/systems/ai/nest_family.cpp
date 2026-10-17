#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "core/random.hpp"
namespace d2x::server::ai {
StepStatus System::nestFamily(EntityId id,UnitTarget target,int distance,TickContext tick,Controller &controller) {
    const auto &nest=*ports_.monsters.find(id);const auto &p=nest.rule.ai.params;
    // master monster_nest / AITHINK_Fn043. Stationary actors do not borrow a
    // walking or melee clock. Birth uses the prepared MonSeq event 4 clock.
    if(distance>20) {controller.nextDecision=tick.tick+25;return StepStatus::Complete;}
    if(controller.act2Attempts>=p[2]) {
        const auto result=perform(controller,[this,id,life=nest.life,tick]{return ports_.monsters.damage(id,id,life,tick.tick);});
        if(isDeferred(result.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
        return StepStatus::Complete;
    }
    if(tick.tick-controller.lastSpawn<uint64_t(p[0])) {
        controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);return StepStatus::Complete;
    }
    // Fn043 consumes its interval even when the birth preflight fails;
    // successful cast attempts, rather than surviving children, exhaust it.
    if(!ports_.monsters.canSpawnNestChild(id)) {
        controller.lastSpawn=tick.tick;controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);
        return StepStatus::Complete;
    }
    const auto result=requestAction(controller,{id,nest.rule.skillIds[0],target,tick.tick});
    if(isDeferred(result.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    if(result) {controller.lastSpawn=tick.tick;++controller.act2Attempts;}
    else controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);
    return StepStatus::Complete;
}
}
