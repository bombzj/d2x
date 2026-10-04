#pragma once
#include "skill_data.hpp"
#include "state_data.hpp"
#include "resources/archive.hpp"

namespace d2x {
void loadBoneSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                    const DataTable &overlays, const DataTable &sounds,
                    const CombatStateCatalog &states, Archives &archives);
}
