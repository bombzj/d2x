#pragma once
#include "content/skills/necromancer_data.hpp"
#include "content/skills/state_data.hpp"
namespace d2x {
struct ClassicData;
void loadRemainingNecromancerSummons(SkillCatalog &, const DataTable &, const DataTable &, const DataTable &, const DataTable &, const DataTable &, const DataTable &, const CombatStateCatalog &, Archives &, const ClassicData &);
}
