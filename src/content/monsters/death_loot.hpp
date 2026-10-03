#pragma once
#include "content/monsters/monster_loot.hpp"
#include "gameplay/loot/plan.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/death_context.hpp"
#include "gameplay/rewards/death.hpp"
#include <cstddef>
#include <set>

namespace d2x {
struct PreparedMonsterDeathLoot {
    MonsterLootEntry entry;
    LootPlan plan;
    size_t treasureDrops = 0; // Original TC diagnostic excludes the extra quest gems.
};
PreparedMonsterDeathLoot prepareMonsterDeathLoot(const ClassicData &data,
    const MonsterCatalog &monsters, const WorldCatalog &world, const LootRequest &request,
    const EnemyDied &death, DeathLootContext context, const std::set<uint32_t> &usedUniques,
    uint64_t &questRandom);
} // namespace d2x
