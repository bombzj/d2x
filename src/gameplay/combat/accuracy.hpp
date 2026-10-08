#pragma once
#include "gameplay/combat/stat_modifiers.hpp"
#include "gameplay/monsters/combat_values.hpp"
#include "gameplay/monsters/rank.hpp"

namespace d2x {
int physicalHitChance(int attackerLevel, int attackRating, int defenderLevel, int defense);
int weaponHitChance(int level, int baseRating, int ratingPercent, const AttackTargetModifiers &modifiers,
                    const MonsterDefense &defense, MonsterRank rank);
int targetDamageBonus(const AttackTargetModifiers &modifiers, const MonsterDefense &defense);
} // namespace d2x
