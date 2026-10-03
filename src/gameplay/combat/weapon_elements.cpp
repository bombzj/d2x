#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
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
    const auto &m = modifiers ? *modifiers : state_.player.attributes.combat;
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
    result.attackerLevel = player.character.level;
    return result;
}
void Simulation::resolveWeaponHit(EntityId defender, float physical, EntityId source,
                                  const AttackElements &originalElements) {
    auto target = combatUnit(defender);
    if (!target.alive() || !target.stats.resolved || !canAttack(source, defender)) return;
    if (!originalElements.smite && target.stats.block > 0 && limitedRandom(*target.random, 100) < unsigned(target.stats.block)) {
        blockUnit(defender);
        if (!originalElements.ranged) skills().triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, source);
        return;
    }
    if (originalElements.selfDamagePercent > 0) {
        DamageRequest self{source, source, float(int64_t(std::min(*target.life, physical) * 256.f) *
            originalElements.selfDamagePercent / 100) / 256.f, MonsterDamageType::Physical};
        self.permission = DamagePermission::ExistingEffect;
        self.hitRecovery = false;
        dealDamage(self);
    }
    auto elements = originalElements;
    auto mitigate = [&](float amount, MonsterDamageType type) {
        const auto damage = resolveIncoming(source, target, amount, type);
        restoreUnit(defender, damage.absorbed);
        return damage.dealt;
    };
    if (elements.crushing) {
        const auto rank = target.stats.rank;
        const bool boss = target.stats.boss || rank == MonsterRank::Unique || rank == MonsterRank::SuperUnique;
        const int divisor = (target.identity.role == CombatRole::Player || target.identity.role == CombatRole::Hireling ? 10 : boss ? 8 : 4) *
                            (elements.ranged ? 2 : 1);
        const float crushing = *target.life / divisor *
            (100 - std::clamp(rawResistance(target.stats.attributes, MonsterDamageType::Physical), 0, 100)) / 100.f;
        *target.life = std::max(1.f / 256.f, *target.life - crushing);
    }
    const float dealtPhysical = mitigate(elements.deadly ? physical * 2.f : physical, MonsterDamageType::Physical);
    skills().healLifeTap(source, defender, dealtPhysical);
    float total = dealtPhysical;
    const int64_t damage = int64_t(std::min(*target.life, dealtPhysical) * 256.f);
    auto leeched = [&](int percent, int divisor) {
        return float(damage * (int64_t(percent) * 64 / std::max(1, divisor)) / 100 * target.stats.drain / 100 / 64) / 256.f;
    };
    restoreUnit(source, leeched(elements.lifeLeech, lifeStealDivisor_), leeched(elements.manaLeech, manaStealDivisor_));
    float chill = 0;
    for (auto [amount, type] : {std::pair{elements.fire, MonsterDamageType::Fire},
             {elements.lightning, MonsterDamageType::Lightning}, {elements.cold, MonsterDamageType::Cold},
             {elements.magic, MonsterDamageType::Magic}}) {
        if (amount <= 0) continue;
        total += mitigate(amount, type);
        if (type == MonsterDamageType::Cold)
            chill = elements.coldDuration * float(std::clamp(100 - rawResistance(target.stats.attributes, type), 0, 200)) / 100.f;
    }
    if (!originalElements.ranged) skills().triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, source);
    if (!originalElements.ranged) skills().reflectThorns(source, defender, dealtPhysical);
    if (!originalElements.ranged) skills().reflectIronMaiden(source, defender, dealtPhysical);
    DamageRequest hit{source, defender, total, MonsterDamageType::Physical, chill, true};
    hit.hitClass = elements.hitClass;
    dealDamage(hit);
    if (target.alive() && total > 0 && elements.openWounds) {
        int framesDamage = 40;
        const int increments[] = {9, 18, 27, 36, 45};
        int remaining = std::max(0, elements.attackerLevel - 1);
        for (int tier = 0; tier < 5 && remaining; ++tier) {
            const int levels = std::min(remaining, tier == 0 ? 14 : tier == 4 ? 99 : 15);
            framesDamage += levels * increments[tier]; remaining -= levels;
        }
        if (target.identity.role == CombatRole::Player) framesDamage /= elements.ranged ? 8 : 4;
        else if (target.stats.rank == MonsterRank::Champion || target.stats.rank == MonsterRank::Unique ||
                 target.stats.rank == MonsterRank::SuperUnique) framesDamage /= 2;
        *target.openWounds.remaining = 8.f;
        *target.openWounds.rate = framesDamage * 25.f / 256.f;
        *target.openWounds.source = source;
    }
    applyPoison(defender, elements.poisonPerSecond, elements.poisonDuration, source);
    if (elements.knockback && target.alive() && !target.stats.boss) applyAuraKnockback(source, defender);
}
void Simulation::applyPoison(EntityId defender, float rawRate, float duration, EntityId source) {
    auto target = combatUnit(defender);
    if (!target.alive() || !canAttack(source, defender) || rawRate <= 0 || duration <= 0 ||
        target.stats.attributes.combat.preventPoison) return;
    const float rate = resolveIncoming(source, target, rawRate / 25.f, MonsterDamageType::Poison).dealt * 25.f;
    const auto &mods = target.stats.attributes.combat;
    duration *= float(std::clamp(100 - mods.poisonLengthResist, 0, 200)) / 100.f;
    if (rate > 0 && rate >= *target.poison.rate && duration > 0) {
        *target.poison.rate = rate; *target.poison.remaining = duration;
        *target.poison.source = source;
    }
}
} // namespace d2x
