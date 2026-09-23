#pragma once
#include "resources/data_table.hpp"
#include <cstdint>
#include <vector>

namespace d2x {
// Index is character level; values are the cumulative XP needed to reach it.
std::vector<uint64_t> barbarianExperienceThresholds(const DataTable &experience);
} // namespace d2x
