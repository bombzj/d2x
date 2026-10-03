#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/monsters/rank.hpp"

namespace d2x {
struct UnitCombatStats {
    CharacterAttributes attributes;
    int level = 1, block = 0, collisionSize = 2, drain = 100;
    int critical = 0, damageRegen = 0, followVelocityBonus = 0;
    float minimumDamage = 0, maximumDamage = 0;
    bool resolved = true;
    bool monsterResistanceRules = false;
    bool demon = false, undead = false, boss = false, primeEvil = false;
    bool freezable = true;
    MonsterRank rank = MonsterRank::Normal;
};
} // namespace d2x
