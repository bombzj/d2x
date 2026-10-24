#pragma once
#include "gameplay/skills/summon_spec.hpp"

namespace d2x {
class SkillFormulaValues;
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty, const std::map<int, int> &hardRanks = {},SkillFormulaValues *formulas=nullptr,int skill=-1,bool pd2=false);
} // namespace d2x
