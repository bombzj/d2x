#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/reward.hpp"
#include <optional>

namespace d2x {
struct LootRequest {
    EntityId source;
    MonsterIdentity identity;
    RegionId region = RegionId::Encampment;
    int difficulty = 0;
    bool questFirstKill = false;
    bool sourceSeed = false; // Unit/chest loot streams must not replace the shared object stream.
    std::optional<MonsterRewardModifiers> rewardModifiers;
    unsigned monsterPlayerCount = 8; // Original admission-time cap; objects use the room count.
};
} // namespace d2x
