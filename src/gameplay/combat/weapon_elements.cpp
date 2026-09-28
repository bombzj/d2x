#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
unsigned roll(uint64_t &random, unsigned limit) {
    return limitedRandom(random, limit);
}
float range(uint64_t &random, int64_t low, int64_t high, int shift = 8) {
    if (high <= 0) return 0;
    low = std::clamp<int64_t>(low * (1 << shift), 0, std::numeric_limits<int>::max());
    high = std::clamp<int64_t>(high * (1 << shift), low, std::numeric_limits<int>::max());
    return float(low + (high > low ? roll(random, unsigned(high - low)) : 0)) / float(1 << shift);
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
AttackElements Simulation::rollAttackElements(EntityId weapon, const CombatModifiers *modifiers,
                                               const SkillCastSpec *skill, uint64_t *randomState) {
    const auto &m = modifiers ? *modifiers : characterStats_.combat;
    auto &player = state_.player;
    auto &random = randomState ? *randomState : player.combatRandom;
    WeaponModifiers own;
    if (auto found = m.weapons.find(weapon); found != m.weapons.end()) own = found->second;
    const auto ranges = attackElementRanges(m, weapon);
    AttackElements result;
    result.fire = range(random, ranges.fire.minimum, ranges.fire.maximum);
    result.lightning = range(random, ranges.lightning.minimum, ranges.lightning.maximum);
    result.cold = range(random, ranges.cold.minimum, ranges.cold.maximum);
    result.magic = range(random, ranges.magic.minimum, ranges.magic.maximum);
    const bool skillPoison = skill && skill->poisonDuration > 0;
    const int64_t poisonMin = int64_t(m.poisonMinimum) + own.poisonMinimum +
                             (skillPoison ? int64_t(skill->minimumDamage * 256.f) : 0);
    const int64_t poisonMax = int64_t(m.poisonMaximum) + own.poisonMaximum +
                             (skillPoison ? int64_t(skill->maximumDamage * 256.f) : 0);
    const int64_t poisonFrames = int64_t(m.poisonFrames) + own.poisonFrames +
                                (skillPoison ? int64_t(skill->poisonDuration * 25.f + .001f) : 0);
    if (poisonMax > 0 && poisonFrames > 0) {
        // MISSILE_AddStatsToDamage adds the skill length but not a poison-source count.
        result.poisonPerSecond = range(random, poisonMin, poisonMax, 0) * 25.f / 256.f;
        result.poisonDuration = float(poisonFrames /
                                std::max<int64_t>(1, int64_t(m.poisonSources) + own.poisonSources)) / 25.f;
    }
    if (result.cold > 0) result.coldDuration = float(int64_t(m.coldFrames) + own.coldFrames) / 25.f;
    const int64_t deadly = int64_t(m.deadlyStrike) + own.deadlyStrike;
    if (deadly > 0) result.deadly = roll(random, 100) < unsigned(std::min<int64_t>(deadly, 100));
    const int crushing = std::clamp(m.crushingBlow + own.crushingBlow, 0, 100);
    const int wounds = std::clamp(m.openWounds + own.openWounds, 0, 100);
    result.crushing = crushing && roll(random, 100) < unsigned(crushing);
    result.openWounds = wounds && roll(random, 100) < unsigned(wounds);
    result.lifeLeech = std::max(0, m.lifeLeech + own.lifeLeech);
    result.manaLeech = std::max(0, m.manaLeech + own.manaLeech);
    result.attackerLevel = player.level;
    return result;
}
void Simulation::resolveWeaponHit(Enemy &enemy, float physical, EntityId source,
                                  const AttackElements &originalElements) {
    auto elements = originalElements;
    if (!elements.playerKillEffects && monsterDefense_)
        if (const auto defense = monsterDefense_(enemy, state_.area.region); defense && defense->boss) {
            auto scale = [&](float value) { return float(int64_t(value * 256.f) * hirelingBossDamagePercent_ / 100) / 256.f; };
            physical = scale(physical); elements.fire = scale(elements.fire);
            elements.cold = scale(elements.cold); elements.lightning = scale(elements.lightning);
            elements.magic = scale(elements.magic); elements.poisonPerSecond = scale(elements.poisonPerSecond / 25.f) * 25.f;
        }
    std::array<int, 6> resistances{};
    for (size_t channel = 0; channel < resistances.size(); ++channel) {
        const auto value = monsterResistance_ ? monsterResistance_(enemy, state_.area.region, MonsterDamageType(channel)) : std::nullopt;
        if (!value) {
            state_.message = "Original monster resistance data is unavailable.";
            return;
        }
        resistances[channel] = *value;
    }
    auto resistance = [&](MonsterDamageType type) {
        return resistances[size_t(type)];
    };
    if (elements.crushing) {
        const auto rank = enemy.identity.rank;
        const bool boss = rank == MonsterRank::Boss || rank == MonsterRank::Unique || rank == MonsterRank::SuperUnique;
        const int divisor = (boss ? 8 : 4) * (elements.ranged ? 2 : 1);
        // Crushing blow uses current HP and ignores negative physical resistance.
        const float crushing = enemy.hp / divisor *
            (100 - std::clamp(resistance(MonsterDamageType::Physical), 0, 100)) / 100.f;
        enemy.hp = std::max(1.f / 256.f, enemy.hp - crushing);
    }
    const float dealtPhysical = mitigateMonsterDamage(elements.deadly ? physical * 2.f : physical,
                                        resistance(MonsterDamageType::Physical));
    float total = dealtPhysical;
    if (source == state_.player.id && monsterDrain_) {
        const int drain = std::max(0, monsterDrain_(enemy));
        const int64_t damage = int64_t(std::min(enemy.hp, dealtPhysical) * 256.f);
        auto leeched = [&](int percent, int divisor) {
            return float(damage * (int64_t(percent) * 64 / std::max(1, divisor)) / 100 * drain / 100 / 64) / 256.f;
        };
        auto &p = state_.player;
        if (elements.playerKillEffects) {
            p.hp = std::min(float(characterStats_.maxLife), p.hp + leeched(elements.lifeLeech, lifeStealDivisor_));
            p.mana = std::min(float(characterStats_.maxMana), p.mana + leeched(elements.manaLeech, manaStealDivisor_));
        } else if (p.hireling.active() && hirelingAttributes_) {
            p.hireling.hp = std::min(float(hirelingAttributes_().maxLife),
                p.hireling.hp + leeched(elements.lifeLeech, lifeStealDivisor_));
        }
    }
    float chill = 0;
    for (auto [amount, type] : {
             std::pair{elements.fire, MonsterDamageType::Fire},
             {elements.lightning, MonsterDamageType::Lightning},
             {elements.cold, MonsterDamageType::Cold},
             {elements.magic, MonsterDamageType::Magic}}) {
        if (amount <= 0) continue;
        const int originalResist = resistance(type);
        const int resist = type == MonsterDamageType::Cold && elements.playerKillEffects && source == state_.player.id &&
                   originalResist < 100 && coldPierce_ ? originalResist - coldPierce_() : originalResist;
        total += mitigateMonsterDamage(amount, resist);
        if (type == MonsterDamageType::Cold && elements.coldDuration > 0) {
            chill = elements.coldDuration * float(std::clamp(100 - resist, 0, 200)) / 100.f;
        }
    }
    damageEnemy(enemy, total, source, chill, false, MonsterDamageType::Physical, true,
                elements.playerKillEffects);
    if (enemy.hp > 0 && total > 0 && elements.openWounds) {
        // D2MOO SKILLITEM_CalculateOpenWoundsHpRegen / EventFunc15; 200 frames.
        int framesDamage = 40;
        const int increments[] = {9, 18, 27, 36, 45};
        int remaining = std::max(0, elements.attackerLevel - 1);
        for (int tier = 0; tier < 5 && remaining; ++tier) {
            const int levels = std::min(remaining, tier == 0 ? 14 : tier == 4 ? 99 : 15);
            framesDamage += levels * increments[tier];
            remaining -= levels;
        }
        if (enemy.identity.rank == MonsterRank::Champion || enemy.identity.rank == MonsterRank::Unique ||
            enemy.identity.rank == MonsterRank::SuperUnique) framesDamage /= 2;
        enemy.openWoundsRemaining = 8.f;
        enemy.openWoundsPerSecond = framesDamage * 25.f / 256.f;
        enemy.openWoundsSource = source;
        enemy.openWoundsPlayerEffects = elements.playerKillEffects;
    }
    applyEnemyPoison(enemy, elements.poisonPerSecond, elements.poisonDuration, source, elements.playerKillEffects);
}
void Simulation::applyEnemyPoison(Enemy &enemy, float rawRate, float duration, EntityId source, bool playerKillEffects) {
    if (enemy.hp <= 0 || rawRate <= 0 || duration <= 0) return;
    const auto resistance = monsterResistance_ ?
        monsterResistance_(enemy, state_.area.region, MonsterDamageType::Poison) : std::nullopt;
    if (!resistance) {
        state_.message = "Original monster poison resistance is unavailable.";
        return;
    }
    // The native poison state stores already mitigated HP regeneration per frame.
    const float rate = mitigateMonsterDamage(rawRate / 25.f, *resistance) * 25.f;
    if (rate > 0 && rate >= enemy.poisonPerSecond) {
        enemy.poisonPerSecond = rate;
        enemy.poisonRemaining = duration;
        enemy.poisonSource = source;
        enemy.poisonPlayerEffects = playerKillEffects;
    }
}
} // namespace d2x
