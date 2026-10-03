#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/reward.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
// Emitted once at the alive -> dead transition, including every fact a loot system needs.
struct EnemyDied {
    EntityId victim, killer;
    MonsterKind kind;
    RegionId region;
    Vec position;
    MonsterIdentity identity;
    int difficulty = 0;
    EntityId attacker; // Actual source; killer is the controlling player receiving credit.
    uint64_t lootRandom = 0;
    int magicFind = 0, goldFind = 0;
    std::optional<MonsterRewardModifiers> rewardModifiers;
    bool noTreasure = false; // Snapshot the original NOTC flag at the same death transition.
};
} // namespace d2x
