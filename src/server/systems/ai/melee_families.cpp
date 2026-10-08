#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
namespace d2x::server::ai {
StepStatus System::meleeFamily(EntityId id, UnitTarget target, Vec position, int size, TickContext tick, Controller &controller) {
    const auto &monster = *ports_.monsters.find(id);
    const auto &area = ports_.areas.at(monster.area);
    const auto distance = meleeDistance(monster.position, monster.rule.size, position, size);
    const bool clear = area.definition.collision.segment(monster.position, position);
    auto move = [&](int stop, int percentage, bool running) {
        return ports_.monsters.requestMove({id, {monster.area, area.generation, position}, target.id, stop, percentage, running});
    };
    // Continue an accepted WL/RN action without rerolling its movement choice.
    // Repath at the old 0.7-second cadence, and finish its own arrival threshold.
    if (controller.pursuing && !monster.route.empty() && monster.movementTarget == target.id &&
        !(distance <= monster.stopDistance && clear)) {
        if (tick.tick >= controller.nextPath) {
            const auto result = move(monster.stopDistance, monster.velocityPercent, monster.running);
            controller.nextPath = tick.tick + 18;
            if (!result) { ports_.monsters.stop(id); controller.pursuing = false; }
        }
        controller.nextDecision = tick.tick + 1;
        return StepStatus::Complete;
    }
    controller.pursuing = false; ports_.monsters.stop(id);
    const auto decision = decideMeleeMonster(monster.rule.ai,
        {distance <= monster.rule.meleeRange && clear, controller.charged,
         missileDistance(monster.position, position), monster.rule.difficulty, controller.random});
    if (!decision) return StepStatus::NotImplemented;
    if (decision->action == MeleeDecisionAction::Attack) {
        const auto result = ports_.skills.requestCast({id, 0, target, tick.tick});
        if (result.status == DomainStatus::Capacity) {
            controller.nextDecision = tick.tick + 1;
            return StepStatus::Blocked; // Preserve the chosen roll and charge through output pressure.
        }
        if (!result) { controller.nextDecision = tick.tick + 1; return StepStatus::Complete; }
    } else if (decision->action == MeleeDecisionAction::Approach) {
        if (!(distance <= decision->stopDistance && clear)) {
            controller.pursuing = bool(move(decision->stopDistance, decision->velocityPercent, decision->running));
            controller.nextPath = tick.tick + 18;
        }
    }
    controller.random = decision->random; controller.charged = decision->charged;
    controller.nextDecision = tick.tick + uint64_t(std::max(1, decision->waitFrames));
    return StepStatus::Complete;
}
}
