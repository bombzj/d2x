#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::triggerCombatEffects(PlayerState &player, CombatEffectEvent event, Enemy &other) {
    // Snapshot before dispatch: future actions may apply or remove other states.
    const auto actions = player.combatEffects.reactions(event, state_.frame);
    for (const auto &trigger : actions)
        std::visit([&](const FreezeAttacker &freeze) {
            const int resistance = monsterResistance_ ?
                monsterResistance_(other, state_.area.region, MonsterDamageType::Cold).value_or(0) : 0;
            const float duration = float(int(freeze.duration * 25) *
                std::clamp(100 - resistance, 0, 200) / 100) / 25.f;
            if (auto freezable = monsterFreezable_ ? monsterFreezable_(other) : std::nullopt) {
                if (*freezable) {
                    other.freeze = std::max(other.freeze, float(int(duration * 25) / monsterFreezeDivisor_) / 25.f);
                    if (other.freeze > 0) other.route.clear();
                } else other.chill = std::max(other.chill, duration);
            }
            if (freeze.overlayId >= 0)
                state_.area.effects.push_back({other.pos, 0, freeze.overlayDuration,
                                              -1, freeze.overlayId, other.id});
        }, trigger.action);
}
} // namespace d2x
