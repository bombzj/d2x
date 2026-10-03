#pragma once
#include "world/object.hpp"
#include "world/level_exit.hpp"
#include "world/plan.hpp"
#include "world/map.hpp"
#include <span>

namespace d2x {
class MonsterCatalog;
struct Region {
    bool loaded = false;
    int staffTombLevel = 0;
    uint64_t objectSeed = 0;
    RegionDefinition definition;
    MapRecipe recipe;
    Map map;
    std::vector<WorldObject> objects;
    std::vector<LevelExit> exits;
    size_t unsupportedObjects = 0;
    void refreshObjectCollision(float time);
};
void configureWorldObject(WorldObject &object, const Table &objectRows);
void linkLevelExits(std::span<Region> regions, const WorldCatalog &catalog);
std::vector<Region> loadRegions(Archives &archives, EntityIds &ids, const std::vector<RegionPlan> &plans,
                                const MonsterCatalog &monsters, const WorldCatalog &catalog,
                                uint32_t mapSeed, uint32_t objectSeed, bool deferred = false);
void loadRegion(Archives &archives, EntityIds &ids, Region &region, TileLibraryCache &cache,
                const MonsterCatalog &monsters, const WorldCatalog &catalog, uint32_t levelSeed);
} // namespace d2x
