#include "accuracy.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
int targetDamageBonus(const AttackTargetModifiers &modifiers, const MonsterDefense &defense) {
    return (defense.demon ? modifiers.demonDamage : 0) + (defense.undead ? modifiers.undeadDamage : 0);
}
int weaponHitChance(int level, int baseRating, int ratingPercent, const AttackTargetModifiers &modifiers,
                    const MonsterDefense &defense, MonsterRank rank) {
    const bool boss = defense.boss || rank == MonsterRank::Boss;
    int armor = defense.defense;
    if (modifiers.ignoreDefense && !boss && !defense.player && !defense.hireling &&
        !nativeUniqueMonster(rank))
        armor = 0;
    int reduction = modifiers.defenseReduction;
    if (reduction > 0) {
        if (boss || defense.player || defense.hireling || rank == MonsterRank::SuperUnique) reduction /= 2;
        armor = nativeSubtract(armor, nativePercentage(armor, std::clamp(reduction, 0, 100), 100));
    }
    auto rating = nativeAdd(baseRating, defense.demon ? modifiers.demonAttackRating : 0);
    rating = nativeAdd(rating, defense.undead ? modifiers.undeadAttackRating : 0);
    rating = nativeAdd(rating, nativePercentage(rating, ratingPercent, 100));
    return physicalHitChance(level, rating, defense.level, armor);
}
int physicalHitChance(int attackerLevel, int attackRating, int defenderLevel, int defense) {
    if (attackerLevel <= 0 || defenderLevel <= 0) return 5;
    auto rating = attackRating;
    auto armor = defense;
    // SUNITDMG_TestHit adjusts the opposing value before clearing negatives.
    if (armor < 0) { rating = nativeSubtract(rating, armor); armor = 0; }
    if (rating < 0) { armor = nativeSubtract(armor, rating); rating = 0; }
    armor = std::max(armor, 0);
    const auto divisor = nativeAdd(rating, armor);
    const auto factor = divisor ? nativeMultiplyDivide(rating, 100, divisor) : 100;
    return std::clamp(nativeDivide(nativeMultiply(nativeMultiply(2, attackerLevel), factor),
        nativeAdd(attackerLevel, defenderLevel)), 5, 95);
}
} // namespace d2x
