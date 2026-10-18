#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::catapultFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    if(int(limitedRandom(controller.random,100))<unit.rule.ai.params[0]) {
        controller.nextDecision=tick.tick+1;
        const auto action=requestAction(controller,{id,0,UnitTarget{},tick.tick,4});
        return isDeferred(action.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+15;
    return StepStatus::Complete;
}
}
