#pragma once
#include "gameplay/monsters/combat_values.hpp"
#include "resources/data_table.hpp"
#include <optional>

namespace d2x {
// Normal profile used by the catalog. Runtime ranks/difficulties are resolved
// by monster_difficulty_combat before hosting prepares the attack rules.
std::optional<MonsterNormalCombat> loadMonsterNormalCombat(const DataTable &stats, size_t row,
                                                          const DataTable *levels);
} // namespace d2x
