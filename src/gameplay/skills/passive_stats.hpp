#pragma once
#include "formula_parameters.hpp"
#include <functional>
#include <set>
namespace d2x {
// Keep the native same-level cache; otherwise replace each existing slot in
// order. After each SetStat the adapter supplies a fresh total snapshot.
bool refreshNativePassiveStats(const SkillFormulaCatalog &,const StatRuleCatalog &,int skill,int rank,
    const std::set<int> &states,SkillFormulaInput &,StatList &,const std::function<void()> &refresh);
int nativeMonsterSkillRank(int base,int allSkills,int nonClassSkill,int singleSkill,int elementSkill,int levelBonus,int maximum);
std::map<int,int> nativeMonsterSkillRanks(const std::map<int,int> &base,const StatSnapshot &,
    const StatRuleCatalog &,const SkillFormulaCatalog &,int levelBonus=0);
}
