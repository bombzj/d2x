#include "accuracy.hpp"
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
    if (modifiers.ignoreDefense && !boss && rank != MonsterRank::Unique && rank != MonsterRank::SuperUnique)
        armor = 0;
    int reduction = modifiers.defenseReduction;
    if (boss || rank == MonsterRank::SuperUnique) reduction /= 2;
    armor -= int(int64_t(armor) * std::clamp(reduction, 0, 100) / 100);
    int64_t rating = int64_t(baseRating) + (defense.demon ? modifiers.demonAttackRating : 0) +
                                         (defense.undead ? modifiers.undeadAttackRating : 0);
    rating += rating * ratingPercent / 100;
    return physicalHitChance(level, int(std::clamp<int64_t>(rating, 0, std::numeric_limits<int>::max())),
                             defense.level, armor);
}
int physicalHitChance(int attackerLevel, int attackRating, int defenderLevel, int defense) {
    if (attackerLevel <= 0 || defenderLevel <= 0) return 5;
    auto rating = std::max<int64_t>(0, attackRating);
    auto divisor = rating + std::max<int64_t>(0, defense);
    auto factor = divisor ? int64_t(100) * rating / divisor : 100;
    return int(std::clamp(int64_t(2) * attackerLevel * factor /
                              (int64_t(attackerLevel) + defenderLevel), int64_t(5), int64_t(95)));
}
} // namespace d2x
