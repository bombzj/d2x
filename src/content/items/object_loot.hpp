#pragma once
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "gameplay/loot/chest.hpp"

namespace d2x {
struct ObjectTreasureEntry {
    std::string treasureClass, deferred;
    int itemLevel = 0;
};
// Chest tiers follow the native per-act area-level thirds. The class itself
// remains a TreasureClassEx record from the mounted MPQ.
ObjectTreasureEntry resolveObjectTreasure(const ClassicData &data, const WorldCatalog &world,
                                               RegionId region, int difficulty);
LootPlan planChestLoot(const ClassicData &data, const ObjectTreasureEntry &entry,
                      const ChestState &chest, int objectClass, uint64_t &objectSeed,
                      const std::set<size_t> &usedUniques, std::string_view characterClass,
                      int magicFind, int goldFind);
LootPlan planAct1RackLoot(const ClassicData &data, const WorldCatalog &world, RegionId region,
                         int difficulty, bool weapon, uint64_t seed);
} // namespace d2x
