#pragma once
#include "gameplay/monsters/combat_values.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <optional>

namespace d2x {
struct MonsterCombatProfile {
    MonsterNormalCombat damage;
    int level = 1;
    std::optional<int> attack1Rating, attack2Rating, defense;
    int criticalChance = 0;
    int damageRegen = 0;
    std::array<int, 6> resistances{};
};
// Resolve the expansion MonStats percentages against the area's MonLvl row.
std::optional<MonsterCombatProfile> loadMonsterCombatProfile(
    const DataTable &stats, size_t row, const DataTable &levels, int difficulty, int areaLevel, std::optional<int> forcedLevel = {});
} // namespace d2x
