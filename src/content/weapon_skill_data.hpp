#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"

namespace d2x {
void loadWeaponSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                      const DataTable &sounds, Archives &archives);
} // namespace d2x
