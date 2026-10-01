#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"

namespace d2x {
void loadBlizzardMissiles(SkillSpec &spec, const DataTable &skills, size_t skillRow,
                         const DataTable &missiles, size_t row, Archives &archives);
} // namespace d2x
