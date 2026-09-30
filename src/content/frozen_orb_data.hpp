#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"

namespace d2x {
void loadFrozenOrbMissiles(SkillSpec &spec, const DataTable &missiles, size_t row, Archives &archives);
} // namespace d2x
