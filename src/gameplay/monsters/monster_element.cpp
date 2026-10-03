#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
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
void Simulation::launchCountessFirewall(Enemy &enemy) {
    const auto position = enemy.skillPosition;
    enemy.skillPosition.reset();
    if (!position || !countessFirewall_ || safeZone_) return;
    const auto &definition = *countessFirewall_;
    const Vec difference = enemy.pos - *position;
    const Vec heading{-difference.y, difference.x};
    skills().launchFirewall(enemy.id, *position, heading.unit(), definition, SkillBehavior::None);

}
void Simulation::prepareMonsterElements(Enemy &enemy, const MonsterNormalCombat &combat, int mode, DamageRequest &hit, int sourceDamage) {
    auto target = combatUnit(hit.defender);
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
            applyPoison(hit.defender, float(int64_t(10 * value) * sourceDamage / 128) * 25.f / 256.f,
                float(2 * slot->durationFrames) / 25.f, enemy.id);
        } else {
            const float chill = slot->type == "cold" ? float(slot->durationFrames * sourceDamage / 128) / 25.f *
                float(std::clamp(100 - rawResistance(target.stats.attributes, MonsterDamageType::Cold), 0, 200)) / 100.f *
                (target.stats.attributes.combat.halfFreezeDuration ? .5f : 1.f) : 0;
            hit.channels[size_t(*type)] += float(int64_t(value) * 256 * sourceDamage / 128) / 256.f;
            hit.chill += chill;
        }
    }
}
void Simulation::resolveMonsterSpell(Enemy &enemy, const Missile &missile, EntityId defender) {
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, missile.monsterAttackMode) : std::nullopt;
    if (!spell || spell->projectile.id != missile.missileId) return;
    if (spell->element == "pois") {
        applyPoison(defender, missile.damage * float(1 << spell->hitShift) * 25.f / 256.f,
            float(spell->poisonFrames) / 25.f, enemy.id);
        return;
    }
    if (auto type = damageType(spell->element)) dealDamage({enemy.id, defender, missile.damage, *type});
}
} // namespace d2x
