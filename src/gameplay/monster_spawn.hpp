#pragma once
#include "definitions.hpp"
#include <cstdint>

namespace d2x {
enum class MonsterRank { Normal, Minion, Champion, Unique, SuperUnique, Boss };
enum class SpawnOrigin { Density, Preset };
struct PopulationSettings {
    uint32_t seed = 0xd2;
    int difficulty = 0; // Normal, Nightmare, Hell. Independent of loot randomness.
};
// Content identity survives implementation substitution, travel, death and saving.
struct MonsterIdentity {
    std::string monster, superUnique, spawnKey;
    MonsterRank rank = MonsterRank::Normal;
    SpawnOrigin origin = SpawnOrigin::Density;
    uint32_t group = 0;
};
struct MonsterSpawn {
    MonsterIdentity identity;
    MonsterKind kind = MonsterKind::Fallen;
    Vec position;
};
struct MonsterImplementation {
    MonsterKind kind;
    bool substitute;
};
// Explicit implementation registry: add real actors here as their behaviour/assets land.
MonsterImplementation monsterImplementation(const std::string &code);
const char *monsterRankName(MonsterRank rank);
} // namespace d2x
