#pragma once
#include "content/classic_data.hpp"
#include "monster_catalog.hpp"
#include "monster_difficulty_combat.hpp"

namespace d2x {
void loadAuraSkills(ClassicData &data);
std::optional<AuraDefinition> resolveAura(const ClassicData &data, int skill, int rank,
    const std::map<int, int> &learned = {}, int fireMasteryPercent = 0,
    int lightningMasteryPercent = 0, int coldMasteryPercent = 0);
bool monsterShrineEligible(const ClassicData &data, const MonsterRecord &monster);
MonsterEnchantment rollMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, uint64_t &random, MonsterRank &rank,
    bool preserveRank = false, bool enableSkillEffects = true);
MonsterEnchantment inheritedMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, const MonsterEnchantment &owner);
MonsterCombatProfile enchantedMonsterCombat(MonsterCombatProfile base, const MonsterEnchantment &mods);
void loadMonsterEnchantmentResources(ClassicData &data, Archives &archives);
} // namespace d2x
