#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
unsigned roll(PlayerState &player, unsigned limit) {
    player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                          (player.combatRandom >> 32);
    return uint32_t(player.combatRandom) % limit;
}
float range(PlayerState &player, int64_t low, int64_t high) {
    if (high <= 0) return 0;
    low = std::clamp<int64_t>(low, 0, std::numeric_limits<int>::max());
    high = std::clamp<int64_t>(high, low, std::numeric_limits<int>::max());
    return float(low + roll(player, unsigned(high - low + 1)));
}
AttackDamageRange boundedRange(int low, int high, int ownLow, int ownHigh) {
    if (int64_t(high) + ownHigh <= 0) return {};
    const auto minimum = std::clamp<int64_t>(int64_t(low) + ownLow, 0,
                                              std::numeric_limits<int>::max());
    const auto maximum = std::clamp<int64_t>(int64_t(high) + ownHigh, minimum,
                                              std::numeric_limits<int>::max());
    return {int(minimum), int(maximum)};
}
} // namespace
AttackElementRanges attackElementRanges(const CombatModifiers &combat, EntityId weapon) {
    WeaponModifiers own;
    if (auto found = combat.weapons.find(weapon); found != combat.weapons.end()) own = found->second;
    return {
        boundedRange(combat.fireMinimum, combat.fireMaximum, own.fireMinimum, own.fireMaximum),
        boundedRange(combat.lightningMinimum, combat.lightningMaximum,
                     own.lightningMinimum, own.lightningMaximum),
        boundedRange(combat.coldMinimum, combat.coldMaximum, own.coldMinimum, own.coldMaximum),
        boundedRange(combat.magicMinimum, combat.magicMaximum, own.magicMinimum, own.magicMaximum)
    };
}
AttackElements Simulation::rollAttackElements(EntityId weapon) {
    const auto &m = characterStats_.combat;
    auto &player = state_.player;
    WeaponModifiers own;
    if (auto found = m.weapons.find(weapon); found != m.weapons.end()) own = found->second;
    const auto ranges = attackElementRanges(m, weapon);
    AttackElements result;
    result.fire = range(player, ranges.fire.minimum, ranges.fire.maximum);
    result.lightning = range(player, ranges.lightning.minimum, ranges.lightning.maximum);
    result.cold = range(player, ranges.cold.minimum, ranges.cold.maximum);
    result.magic = range(player, ranges.magic.minimum, ranges.magic.maximum);
    if (int64_t(m.poisonMaximum) + own.poisonMaximum > 0 &&
        int64_t(m.poisonFrames) + own.poisonFrames > 0) {
        result.poisonPerSecond = range(player, int64_t(m.poisonMinimum) + own.poisonMinimum,
                                       int64_t(m.poisonMaximum) + own.poisonMaximum) * 25.f / 256.f;
        result.poisonDuration = float(int64_t(m.poisonFrames) + own.poisonFrames) /
                                float(std::max<int64_t>(1, int64_t(m.poisonSources) + own.poisonSources) * 25);
    }
    if (result.cold > 0) result.coldDuration = float(int64_t(m.coldFrames) + own.coldFrames) / 25.f;
    const int64_t deadly = int64_t(m.deadlyStrike) + own.deadlyStrike;
    if (deadly > 0) result.deadly = roll(player, 100) < unsigned(std::min<int64_t>(deadly, 100));
    return result;
}
void Simulation::resolveWeaponHit(Enemy &enemy, float physical, EntityId source,
                                  const AttackElements &elements) {
    auto resistance = [&](MonsterDamageType type) {
        return monsterResistance_
            ? monsterResistance_(enemy, state_.area.region, type).value_or(0) : 0;
    };
    float total = mitigateMonsterDamage(elements.deadly ? physical * 2.f : physical,
                                        resistance(MonsterDamageType::Physical));
    float chill = 0;
    for (auto [amount, type] : {
             std::pair{elements.fire, MonsterDamageType::Fire},
             {elements.lightning, MonsterDamageType::Lightning},
             {elements.cold, MonsterDamageType::Cold},
             {elements.magic, MonsterDamageType::Magic}}) {
        if (amount <= 0) continue;
        const int resist = resistance(type);
        total += mitigateMonsterDamage(amount, resist);
        if (type == MonsterDamageType::Cold && elements.coldDuration > 0) {
            chill = elements.coldDuration * float(std::clamp(100 - resist, 0, 200)) / 100.f;
        }
    }
    damageEnemy(enemy, total, source, chill, false, MonsterDamageType::Physical, true);
    if (enemy.hp <= 0 || elements.poisonPerSecond <= 0 || elements.poisonDuration <= 0) return;
    const float rate = resistance(MonsterDamageType::Poison) >= 100 ? 0.f : elements.poisonPerSecond;
    if (rate > 0 && rate >= enemy.poisonPerSecond) {
        enemy.poisonPerSecond = rate;
        enemy.poisonRemaining = elements.poisonDuration;
        enemy.poisonSource = source;
    }
}
} // namespace d2x
