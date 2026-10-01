#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include "resources/data_table.hpp"
#include <optional>
#include <string_view>

namespace d2x {
std::optional<MonsterAiProfile> loadMonsterAiProfile(const DataTable &stats, size_t row,
                                                     std::string_view ai, int difficulty);
} // namespace d2x
