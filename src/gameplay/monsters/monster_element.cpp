#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <optional>
#include <string_view>

namespace d2x {
namespace {
unsigned roll(Enemy &enemy, unsigned limit) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom) % limit;
}
std::optional<int> resistance(const CharacterAttributes &stats, int penalty,
                              std::string_view type) {
    int value = 0;
    if (type == "fire") value = stats.fireResist + penalty;
    else if (type == "ltng") value = stats.lightningResist + penalty;
    else if (type == "cold") value = stats.coldResist + penalty;
    else if (type == "pois") value = stats.poisonResist + penalty;
    else if (type != "mag" && type != "mana") return std::nullopt;
    return std::clamp(value, -100, 75);
}
} // namespace
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode) {
    auto &player = state_.player;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (mode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        const auto defense = resistance(characterStats_, resistancePenalty_, slot->type);
        if (!defense) continue; // Other original effect families await their shared handlers.
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        if (slot->type == "mana") {
            player.mana = std::max(0.f, player.mana - float(value));
            continue;
        }
        if (slot->type == "pois") {
            // D2MOO writes 10 * elemental damage as HP regeneration units per
            // frame (256 units per HP), for twice the MonStats duration.
            const float rate = float(10 * value * 25) / 256.f * float(100 - *defense) / 100.f;
            const float length = float(2 * slot->durationFrames) / 25.f;
            if (rate > 0 && length > 0 && player.poisonPerSecond <= rate) {
                player.poisonPerSecond = rate;
                player.poisonRemaining = length;
            }
            continue;
        }
        player.hp = std::max(0.f, player.hp - float(value) * float(100 - *defense) / 100.f);
        if (slot->type == "cold" && slot->durationFrames > 0) {
            const float length = float(slot->durationFrames) * float(100 - *defense) / 2500.f;
            player.chill = std::max(player.chill, length);
        }
    }
}
void Simulation::resolveMonsterSpell(Enemy &enemy, const Missile &missile) {
    auto &player = state_.player;
    if (player.dead || player.hp <= 0 || player.leapTime > 0) return;
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, missile.hostileMode) : std::nullopt;
    if (!spell || spell->projectile.id != missile.missileId) return;
    const auto defense = resistance(characterStats_, resistancePenalty_, spell->element);
    if (!defense) return;
    player.hp = std::max(0.f, player.hp - missile.damage * float(100 - *defense) / 100.f);
    player.hitTime = .16f;
}
} // namespace d2x
