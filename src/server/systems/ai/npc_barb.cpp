#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2x::server::ai {
StepStatus System::npcBarbFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto registered=ports_.monsters.registerWanderTarget(id);
    if(!registered) return isDeferred(registered.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;
    controller.nextDecision=tick.tick+1;
    controller.pursuing=false;
    const auto *room=area.definition.activation.room(unit.position);
    const auto target=acquireTarget(unit,tick,false,room && !room->checkLosDraw && !controller.acquiredTarget);
    controller.target=target?std::optional{target->unit.id}:std::nullopt;
    if(target) {
        controller.acquiredTarget=true;
        if(unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,target->position,target->size,unit.rule.meleeRange)) {
            controller.nextDecision=tick.tick+uint64_t(std::max(1,params[0]));
            const auto attack=requestAction(controller,{id,0,target->unit,tick.tick,4});
            return isDeferred(attack.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        if(target->distance<params[2] && int(rollRandom(controller.random)%100)<params[1]) {
            bool run=true;
            const auto curse=unit.rule.formulas->skillIds.find("decrepify");
            if(curse!=unit.rule.formulas->skillIds.end()) {
                const int state=unit.rule.formulas->skills.at(curse->second).auraTargetState;
                run=state<0 || !ports_.effects.unitStates(id,tick.tick).contains(state);
            }
            const auto move=ports_.monsters.requestMove({id,{unit.area,area.generation,target->position},target->unit.id,0,run?175:75,run,1});
            controller.pursuing=bool(move);
            return isDeferred(move.status)?StepStatus::Blocked:StepStatus::Complete;
        }
    }
    const Vec origin{std::floor(unit.position.x)+.5f,std::floor(unit.position.y)+.5f};
    const auto draw=[](uint64_t &random){return int(rollRandom(random)%20);};
    const auto walk=[&](int xShift,int yShift) {
        // D2MOO's argument evaluation order is unspecified; retail Fn127
        // confirms X first, then Y, and no retry draws until a failed path.
        const int x=draw(controller.random)+xShift;
        const int y=draw(controller.random)+yShift;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,origin+Vec{float(x),float(y)}},{},0,75,false,1});
        controller.pursuing=bool(moved);return moved;
    };
    auto moved=walk(-40,-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    const bool flip=(rollRandom(controller.random)&1)!=0;
    moved=walk(-10,-30*(2*int(flip)-1)-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    moved=walk(-10,-30*(2*int(!flip)-1)-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+15;
    return StepStatus::Complete;
}
}
