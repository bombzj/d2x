#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "gameplay/monsters/monster_spawn.hpp"

namespace d2x {
std::array<std::optional<MonsterSpell>, 4> loadMonsterSpells(
    Archives &archives, const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &missiles);
} // namespace d2x
