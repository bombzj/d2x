#pragma once
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/reward.hpp"
#include "world/identity.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace d2x {
struct ClassicData;
class MonsterCatalog;
class WorldCatalog;
struct MonsterExperienceRequest {
    MonsterIdentity identity;
    RegionId region = RegionId::Encampment;
    int difficulty = 0, playerLevel = 1;
    std::optional<MonsterRewardModifiers> rewardModifiers;
};
struct MonsterExperienceAward {
    uint64_t amount = 0;
    int monsterLevel = 0;
    std::string deferred;
    uint64_t base{};
    int ratio{},shift{};
};
MonsterExperienceAward resolveMonsterExperience(const ClassicData &data, const MonsterCatalog &monsters,
                                               const WorldCatalog &world,
                                               const MonsterExperienceRequest &request);
} // namespace d2x
