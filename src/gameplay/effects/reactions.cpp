#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::triggerCombatEffects(EntityId target, CombatEffectEvent event, EntityId other) {
    const auto unit = combatUnit(target), attacker = combatUnit(other);
    if (!unit || !attacker.alive()) return;
    const auto actions = unit.effects->reactions(event, state_.frame);
    for (const auto &trigger : actions)
        std::visit([&](const FreezeAttacker &freeze) {
            const float duration = float(int(freeze.duration * 25) *
                std::clamp(100 - unitResistance(attacker, MonsterDamageType::Cold), 0, 200) / 100) / 25.f;
            applyChill(other, duration, true);
            if (freeze.overlayId >= 0)
                state_.area.effects.push_back({*attacker.position, 0, freeze.overlayDuration, -1, freeze.overlayId, other});
        }, trigger.action);
}
} // namespace d2x
