#include "gameplay/simulation/simulation.hpp"
#include "damage_resolution.hpp"
#include <algorithm>

namespace d2x {
float Simulation::hurtPlayer(float amount, MonsterDamageType type) {
    auto &player = state_.player;
    if (player.dead || player.hp <= 0) return 0;
    const auto resolved = mitigatePlayerDamage(amount, type, characterStats_);
    player.hp = std::min(float(characterStats_.maxLife), player.hp + resolved.absorbed);
    const float dealt = std::min(player.hp, resolved.dealt);
    player.hp = std::max(0.f, player.hp - resolved.dealt);
    if (dealt > 0) player.hitTime = .16f;
    return dealt;
}
} // namespace d2x
