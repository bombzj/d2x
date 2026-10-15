#pragma once
#include "gameplay/monsters/unique_modifiers.hpp"
#include "gameplay/monsters/identity.hpp"
#include "content/classic_data.hpp"
#include "monster_catalog.hpp"
#include "monster_difficulty_combat.hpp"

namespace d2x {
bool monsterShrineEligible(const ClassicData &data, const MonsterRecord &monster);
std::vector<MonsterModifierChoice> prepareMonsterModifierChoices(const ClassicData &,const MonsterRecord &,int difficulty,bool champion);
MonsterEnchantment resolveMonsterEnchantment(const ClassicData &,const MonsterRecord &,const MonsterCombatProfile &,
    int difficulty,MonsterRank,std::vector<int> modifiers,uint16_t nameSeed,bool enableSkillEffects=true,
    const SuperUniqueRecord *fixed=nullptr);
MonsterEnchantment rollMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, uint64_t &random, MonsterRank &rank,
    bool preserveRank = false, bool enableSkillEffects = true, bool championVariantAllowed = true,
    const SuperUniqueRecord *fixed = nullptr);
MonsterEnchantment inheritedMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, const MonsterEnchantment &owner);
MonsterCombatProfile enchantedMonsterCombat(MonsterCombatProfile base, const MonsterEnchantment &mods);
void loadMonsterEnchantmentResources(ClassicData &data, Archives &archives);
std::string monsterDisplayName(const ClassicData &data, const MonsterIdentity &identity, std::string_view species,
    const MonsterEnchantment *enchantment);
std::string monsterModifierDescription(const ClassicData &data, const MonsterIdentity &identity,
    const MonsterEnchantment *enchantment);
} // namespace d2x
