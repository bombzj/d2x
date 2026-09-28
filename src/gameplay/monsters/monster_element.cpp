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
void Simulation::applyMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode, EntityId defender, bool recovery) {
    auto target = combatUnit(defender);
    if (!target.alive()) return;
    for (const auto &slot : combat.elements) {
        if (!slot || slot->mode != (mode == 2 ? "A2" : "A1")) continue;
        if (slot->chance < 100 && roll(enemy, 100) >= unsigned(slot->chance)) continue;
        const auto type = damageType(slot->type);
        if (!type && slot->type != "mana") continue;
        const int value = slot->minimum + int(roll(enemy, unsigned(slot->maximum - slot->minimum + 1)));
        if (slot->type == "mana") {
            if (target.mana) *target.mana = std::max(0.f, *target.mana - float(value));
        } else if (slot->type == "pois") {
            applyPoison(defender, float(10 * value * 25) / 256.f, float(2 * slot->durationFrames) / 25.f, enemy.id);
        } else {
            const float chill = slot->type == "cold" ? float(slot->durationFrames) / 25.f *
                float(std::clamp(100 - unitResistance(target, MonsterDamageType::Cold), 0, 200)) / 100.f *
                (target.stats.attributes.combat.halfFreezeDuration ? .5f : 1.f) : 0;
            dealDamage({enemy.id, defender, float(value), *type, chill, false, recovery});
        }
    }
}
void Simulation::resolveMonsterSpell(Enemy &enemy, const Missile &missile, EntityId defender) {
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, missile.monsterAttackMode) : std::nullopt;
    if (!spell || spell->projectile.id != missile.missileId) return;
    if (auto type = damageType(spell->element)) dealDamage({enemy.id, defender, missile.damage, *type});
}
} // namespace d2x
