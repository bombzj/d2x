#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"

namespace d2x {
void loadSkillAnimations(SkillCatalog &catalog, const DataTable &weapons, Archives &archives);
}
