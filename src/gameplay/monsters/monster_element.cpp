#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
namespace {
unsigned roll(Enemy &enemy, unsigned limit) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom) % limit;
}
} // namespace
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat) {
    auto &player = state_.player;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (enemy.attackMode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        int resistance = 0;
        if (slot->type == "fire") resistance = characterStats_.fireResist + resistancePenalty_;
        else if (slot->type == "ltng") resistance = characterStats_.lightningResist + resistancePenalty_;
        else if (slot->type == "cold") resistance = characterStats_.coldResist + resistancePenalty_;
        else if (slot->type == "mag") resistance = 0;
        else continue; // Other original effect families await their shared handlers.
        resistance = std::clamp(resistance, -100, 75);
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        player.hp = std::max(0.f, player.hp - float(value) * float(100 - resistance) / 100.f);
        if (slot->type == "cold" && slot->durationFrames > 0) {
            const float length = float(slot->durationFrames) * float(100 - resistance) / 2500.f;
            player.chill = std::max(player.chill, length);
        }
    }
}
} // namespace d2x
