#pragma once
#include "gameplay/model/definitions.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace d2x {
enum class MonsterRank { Normal, Minion, Champion, Unique, SuperUnique, Boss };
enum class SpawnOrigin { Density, Preset, Debug };
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
struct MonsterAccuracy {
    int level = 1;
    int attackRating = 0;
};
struct MonsterDefense {
    int level = 1;
    int defense = 0;
};
enum class MonsterDamageType { Physical, Magic, Fire, Lightning, Cold, Poison };
// Resolved from the original MonStats and MonLvl tables. Some ordinary monsters
// have life but no A1 melee columns, so the attacks are independent.
struct MonsterNormalCombat {
    int minLife = 0, maxLife = 0;
    std::optional<std::pair<int, int>> attack1Damage;
    std::optional<std::pair<int, int>> attack2Damage;
};
enum class MonsterAiKind { Skeleton, Brute, Zombie, Fallen };
struct MonsterAiProfile {
    MonsterAiKind kind;
    std::array<int, 8> params{};
};
struct MonsterAttackTiming {
    float duration = 0;
    float impact = 0;
    int frames = 0;
};
// Explicit implementation registry: add real actors here as their behaviour/assets land.
MonsterImplementation monsterImplementation(const std::string &code);
const char *monsterRankName(MonsterRank rank);
} // namespace d2x
