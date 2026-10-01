#pragma once
#include "content/classic_data.hpp"
#include "monster_catalog.hpp"
#include "content/world/world_catalog.hpp"

namespace d2x {
struct MonsterExperienceRequest {
    MonsterIdentity identity;
    RegionId region = RegionId::Encampment;
    int difficulty = 0, playerLevel = 1;
};
struct MonsterExperienceAward {
    uint64_t amount = 0;
    int monsterLevel = 0;
    std::string deferred;
};
MonsterExperienceAward resolveMonsterExperience(const ClassicData &data, const MonsterCatalog &monsters,
                                               const WorldCatalog &world,
                                               const MonsterExperienceRequest &request);
} // namespace d2x
