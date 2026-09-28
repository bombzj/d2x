#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <optional>
#include <string_view>

namespace d2x {
namespace {
unsigned roll(Enemy &enemy, unsigned limit) {
    rollRandom(enemy.combatRandom);
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
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode, bool hitHireling) {
    auto &player = state_.player;
    auto &merc = player.hireling;
    const auto stats = hitHireling ? hirelingAttributes_() : characterStats_;
    auto &poisonRate = hitHireling ? merc.poisonPerSecond : player.poisonPerSecond;
    auto &poisonTime = hitHireling ? merc.poisonRemaining : player.poisonRemaining;
    auto &chill = hitHireling ? merc.chill : player.chill;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (mode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        const auto type = damageType(slot->type);
        if (!type && slot->type != "mana") continue;
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        if (slot->type == "mana") {
            if (!hitHireling) player.mana = std::max(0.f, player.mana - float(value));
            continue;
        }
        if (slot->type == "pois") {
            if (stats.combat.preventPoison) continue;
            // D2MOO writes 10 * elemental damage as HP regeneration units per
            // frame (256 units per HP), for twice the MonStats duration.
            const float rate = (hitHireling ? hirelingIncomingDamage(enemy, float(10 * value * 25)) : float(10 * value * 25)) / 256.f *
                               float(100 - stats.poisonResist) / 100.f;
            const float length = float(2 * slot->durationFrames) / 25.f *
                                 float(std::clamp(100 - stats.combat.poisonLengthResist,
                                                  0, 200)) / 100.f;
            if (rate > 0 && length > 0 && poisonRate <= rate) {
                poisonRate = rate;
                poisonTime = length;
            }
            continue;
        }
        if (hitHireling) hurtHireling(hirelingIncomingDamage(enemy, float(value)), *type, false);
        else hurtPlayer(float(value), *type);
        if (slot->type == "cold" && slot->durationFrames > 0) {
            if (!stats.combat.cannotBeFrozen) {
                const float length = float(slot->durationFrames) *
                    float(100 - stats.coldResist) / 2500.f *
                    (stats.combat.halfFreezeDuration ? .5f : 1.f);
                chill = std::max(chill, length);
            }
        }
    }
}
void Simulation::resolveMonsterSpell(Enemy &enemy, const Missile &missile, bool hitHireling) {
    auto &player = state_.player;
    if (hitHireling ? !player.hireling.active() : (player.dead || player.hp <= 0)) return;
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, missile.hostileMode) : std::nullopt;
    if (!spell || spell->projectile.id != missile.missileId) return;
    const auto type = damageType(spell->element);
    if (!type) return;
    if (hitHireling) hurtHireling(hirelingIncomingDamage(enemy, missile.damage), *type);
    else hurtPlayer(missile.damage, *type);
}
} // namespace d2x
