#pragma once
#include "skill_data.hpp"
#include "state_data.hpp"
namespace d2x {
class Archives;
void loadAmazonMagicSkills(SkillCatalog &, const DataTable &, const DataTable &, const DataTable &,
                          const CombatStateCatalog &, Archives &);
void applyAmazonPassives(CharacterModifiers &, const SkillCatalog &, const std::map<int, int> &,
                         int classRow, std::string_view classCode);
}
