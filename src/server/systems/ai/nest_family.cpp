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
    if(nest.nestSpawned>=p[2]) {
        const auto result=ports_.monsters.damage(id,id,nest.life,tick.tick);
        if(result.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
        return StepStatus::Complete;
    }
    if(tick.tick-controller.lastSpawn<uint64_t(p[0])) {
        controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);return StepStatus::Complete;
    }
    const auto result=ports_.skills.requestCast({id,nest.rule.skillIds[0],target,tick.tick});
    if(result.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    if(result) controller.lastSpawn=tick.tick;
    else controller.nextDecision=tick.tick+20+limitedRandom(controller.random,10);
    return StepStatus::Complete;
}
}
