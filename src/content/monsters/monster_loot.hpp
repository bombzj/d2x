#pragma once
#include "content/classic_data.hpp"
#include "monster_catalog.hpp"
#include "content/world/world_catalog.hpp"

namespace d2x {
enum class LootEntryStatus { Ready, Empty, Deferred };
struct MonsterLootEntry {
    LootEntryStatus status = LootEntryStatus::Deferred;
    std::string treasureClass, reason;
    int itemLevel = 0, upgradeLevel = 0;
};
MonsterLootEntry resolveMonsterLoot(const ClassicData &data, const MonsterCatalog &monsters,
                                  const WorldCatalog &world, const LootRequest &request);
} // namespace d2x