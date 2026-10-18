#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::hellMeteorFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &params=unit.rule.ai.params;
    if(unit.rule.skillActions.contains(unit.rule.skillIds[0]) && int(limitedRandom(controller.random,100))<params[0]) {
        const auto span=uint32_t(nativeMultiply(2,params[2]));
        const auto x=nativeSubtract(int32_t(limitedRandom(controller.random,span)),params[2]);
        const auto y=nativeSubtract(int32_t(limitedRandom(controller.random,span)),params[2]);
        const Vec point{std::floor(unit.position.x)+float(x)+.5f,std::floor(unit.position.y)+float(y)+.5f};
        const auto skill=unit.rule.skillIds[0];const auto found=unit.rule.skillActions.find(skill);
        if(found==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        controller.nextDecision=tick.tick+1;
        const auto cast=requestAction(controller,{id,skill,UnitTarget{},tick.tick,found->second.nativeMode,point});
        return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+uint64_t(params[1]);
    return StepStatus::Complete;
}
}
