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
std::optional<MonsterDamageType> damageType(std::string_view type) {
    if (type == "fire") return MonsterDamageType::Fire;
    if (type == "ltng") return MonsterDamageType::Lightning;
    if (type == "cold") return MonsterDamageType::Cold;
    if (type == "pois") return MonsterDamageType::Poison;
    if (type == "mag") return MonsterDamageType::Magic;
    return std::nullopt;
}
} // namespace
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode) {
    auto &player = state_.player;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (mode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        const auto type = damageType(slot->type);
        if (!type && slot->type != "mana") continue;
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        if (slot->type == "mana") {
            player.mana = std::max(0.f, player.mana - float(value));
            continue;
        }
        if (slot->type == "pois") {
            // D2MOO writes 10 * elemental damage as HP regeneration units per
            // frame (256 units per HP), for twice the MonStats duration.
            const float rate = float(10 * value * 25) / 256.f *
                               float(100 - characterStats_.poisonResist) / 100.f;
            const float length = float(2 * slot->durationFrames) / 25.f *
                                 float(std::clamp(100 - characterStats_.combat.poisonLengthResist,
                                                  0, 200)) / 100.f;
            if (rate > 0 && length > 0 && player.poisonPerSecond <= rate) {
                player.poisonPerSecond = rate;
                player.poisonRemaining = length;
            }
            continue;
        }
        hurtPlayer(float(value), *type);
        if (slot->type == "cold" && slot->durationFrames > 0) {
            if (!characterStats_.combat.cannotBeFrozen) {
                const float length = float(slot->durationFrames) *
                    float(100 - characterStats_.coldResist) / 2500.f *
                    (characterStats_.combat.halfFreezeDuration ? .5f : 1.f);
                player.chill = std::max(player.chill, length);
            }
        }
    }
}
void Simulation::resolveMonsterSpell(Enemy &enemy, const Missile &missile) {
    auto &player = state_.player;
    if (player.dead || player.hp <= 0 || player.leapTime > 0) return;
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, missile.hostileMode) : std::nullopt;
    if (!spell || spell->projectile.id != missile.missileId) return;
    const auto type = damageType(spell->element);
    if (!type) return;
    hurtPlayer(missile.damage, *type);
}
} // namespace d2x
