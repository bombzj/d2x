#pragma once
#include "gameplay/monsters/rank.hpp"
#include <cstdint>
#include <string>

namespace d2x {
enum class SpawnOrigin { Density, Preset, Debug, Summoned };
// Content identity survives implementation substitution, travel and death.
// Runtime combat modifiers are owned separately by the live actor.
struct MonsterIdentity {
    std::string monster, superUnique, spawnKey;
    MonsterRank rank = MonsterRank::Normal;
    SpawnOrigin origin = SpawnOrigin::Density;
    uint32_t group = 0;
    std::string ownerSpawnKey = {}; // Original setboss party ownership within this region.
    bool championVariantAllowed = true;
};
} // namespace d2x
