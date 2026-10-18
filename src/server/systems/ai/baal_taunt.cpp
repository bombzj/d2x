#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/arithmetic.hpp"

namespace d2x::server::ai {
StepStatus System::baalTauntFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &params=unit.rule.ai.params;
    const auto target=acquireTarget(unit,tick,false,false);
    const auto idle=[&] {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+25;};
    if(!target) {idle();return StepStatus::Complete;}
    bool neutral=false;
    if(const auto *monster=ports_.monsters.find(target->unit.id)) neutral=monster->nativeMode==1;
    else if(const auto *player=ports_.players.findActor(target->unit.id))
        neutral=!player->moving && player->route.empty() && !ports_.skills.busy(player->actor,tick.tick);
    if(!neutral) controller.phase=0;
    else {
        controller.phase=nativeAdd(controller.phase,1);
        if(controller.phase>params[1]) {
            controller.phase=0;controller.nextDecision=tick.tick+1;
            const auto cast=requestAction(controller,{id,unit.rule.skillIds[0],target->unit,tick.tick,4});
            return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
        }
    }
    if(target->distance<=params[0]) {idle();return StepStatus::Complete;}
    const auto result=perform(controller,[this,id,at=target->position,victim=target->unit.id,far=target->distance>params[2],tick]() ->DomainResult<> {
        const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        auto &ai=state_.controllers.at(id);
        if(far) {
            // Fn136 calls SUnit's free-coordinate placement directly. This is
            // movement of the invisible controller, without Teleport skill FX.
            const auto moved=ports_.monsters.nativeTeleport(id,at,tick.tick,ports_.spatial.nativeCollisionSample());
            if(isDeferred(moved.status)) return moved;
            if(moved) {
                ports_.monsters.neutralMode(id);ai.pursuing=false;ai.nextDecision=tick.tick+25;return moved;
            }
        }
        ai.nextDecision=tick.tick+uint64_t(unit->rule.neutralDelayTicks);
        const auto moved=ports_.monsters.requestMove({id,{unit->area,ports_.areas.at(unit->area).generation,at},victim,0,75,false,1});
        ai.pursuing=bool(moved);return moved;
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
