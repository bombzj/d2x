#pragma once
#include "classic_data.hpp"
#include "monster_catalog.hpp"
#include "monster_difficulty_combat.hpp"

namespace d2x {
bool monsterShrineEligible(const ClassicData &data, const MonsterRecord &monster);
MonsterEnchantment rollMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, uint64_t &random, MonsterRank &rank);
MonsterEnchantment inheritedMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, const MonsterEnchantment &owner);
MonsterCombatProfile enchantedMonsterCombat(MonsterCombatProfile base, const MonsterEnchantment &mods);
void loadMonsterEnchantmentResources(ClassicData &data, Archives &archives);
} // namespace d2x
