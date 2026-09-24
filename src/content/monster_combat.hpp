#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include "resources/data_table.hpp"
#include <optional>

namespace d2x {
// LoD single-player, normal-difficulty A1 baseline. Other ranks and
// difficulties retain their explicit pending implementation.
std::optional<MonsterNormalCombat> loadMonsterNormalCombat(const DataTable &stats, size_t row,
                                                          const DataTable *levels);
} // namespace d2x
