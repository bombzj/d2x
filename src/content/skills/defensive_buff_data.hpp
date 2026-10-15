#pragma once
#include "skill_data.hpp"
#include "state_data.hpp"
#include "resources/archive.hpp"
namespace d2x {
void loadNativeDefensiveBuffs(SkillCatalog &,const DataTable &,const DataTable &,const DataTable &,const CombatStateCatalog &,Archives &);
}
