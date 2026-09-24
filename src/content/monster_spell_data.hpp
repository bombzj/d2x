#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "gameplay/monsters/monster_spawn.hpp"

namespace d2x {
std::array<std::optional<MonsterSpell>, 4> loadMonsterSpells(
    Archives &archives, const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &missiles, const DataTable &sequences);
std::optional<MonsterResurrection> loadMonsterResurrection(
    const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &sequences);
std::optional<MonsterNest> loadMonsterNest(
    const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &sequences);
} // namespace d2x
