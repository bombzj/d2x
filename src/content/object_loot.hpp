#pragma once
#include "classic_data.hpp"
#include "world_catalog.hpp"

namespace d2x {
struct ObjectTreasureEntry {
    std::string treasureClass, deferred;
    int itemLevel = 0;
};
// Act I's chest tier follows the native area-level thirds. The class itself
// remains a TreasureClassEx record from the mounted MPQ.
ObjectTreasureEntry resolveAct1ObjectTreasure(const ClassicData &data, const WorldCatalog &world,
                                               RegionId region, int difficulty);
LootPlan planAct1RackLoot(const ClassicData &data, const WorldCatalog &world, RegionId region,
                         int difficulty, bool weapon, uint64_t seed);
} // namespace d2x
