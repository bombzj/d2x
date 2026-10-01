#pragma once
#include "resources/data_table.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace d2x {
// Index is character level; values are the cumulative XP needed to reach it.
std::vector<uint64_t> experienceThresholds(const DataTable &experience, std::string_view character);
} // namespace d2x
