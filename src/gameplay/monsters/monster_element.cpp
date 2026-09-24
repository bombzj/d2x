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
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode) {
    auto &player = state_.player;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (mode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        int resistance = 0;
        if (slot->type == "fire") resistance = characterStats_.fireResist + resistancePenalty_;
        else if (slot->type == "ltng") resistance = characterStats_.lightningResist + resistancePenalty_;
        else if (slot->type == "cold") resistance = characterStats_.coldResist + resistancePenalty_;
        else if (slot->type == "pois") resistance = characterStats_.poisonResist + resistancePenalty_;
        else if (slot->type == "mag" || slot->type == "mana") resistance = 0;
        else continue; // Other original effect families await their shared handlers.
        resistance = std::clamp(resistance, -100, 75);
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        if (slot->type == "mana") {
            player.mana = std::max(0.f, player.mana - float(value));
            continue;
        }
        if (slot->type == "pois") {
            // D2MOO writes 10 * elemental damage as HP regeneration units per
            // frame (256 units per HP), for twice the MonStats duration.
            const float rate = float(10 * value * 25) / 256.f * float(100 - resistance) / 100.f;
            const float length = float(2 * slot->durationFrames) / 25.f;
            if (rate > 0 && length > 0 && player.poisonPerSecond <= rate) {
                player.poisonPerSecond = rate;
                player.poisonRemaining = length;
            }
            continue;
        }
        player.hp = std::max(0.f, player.hp - float(value) * float(100 - resistance) / 100.f);
        if (slot->type == "cold" && slot->durationFrames > 0) {
            const float length = float(slot->durationFrames) * float(100 - resistance) / 2500.f;
            player.chill = std::max(player.chill, length);
        }
    }
}
} // namespace d2x
