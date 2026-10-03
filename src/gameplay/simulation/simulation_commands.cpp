#include "simulation.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/caster.hpp"
#include <type_traits>

namespace d2x {
void Simulation::execute(const GameCommand &command) {
    if (!grid_)
        return;
    std::visit(
        [this](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveTo>)
                moveTo(intent.position);
            else if constexpr (std::is_same_v<T, Attack>)
                requestAttack(intent);
            else if constexpr (std::is_same_v<T, DebugKill>) {
                if (!state_.player.actions.dead)
                    if (auto enemy = findEnemy(intent.target);
                        enemy && enemy->hp > 0 && (intent.ignoreActivation || active(enemy->pos)))
                        damageEnemy(*enemy, enemy->hp, state_.player.id, 0, intent.ignoreActivation);
            }
            else if constexpr (std::is_same_v<T, ToggleRun>)
                state_.player.movement.running = !state_.player.movement.running;
            else if constexpr (std::is_same_v<T, StopMoving>)
                stopWalking();
            else if constexpr (std::is_same_v<T, StopChannel>)
                skills().stopChannel(skillCaster(state_.player.id));
        },
        command);
}
} // namespace d2x
