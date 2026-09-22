#pragma once
#include "content/monster_catalog.hpp"
#include "gameplay/monster_spawn.hpp"
#include "map.hpp"
#include <iosfwd>

namespace d2x {
struct PopulationPlan {
    std::vector<MonsterSpawn> spawns;
    std::vector<std::string> roster, diagnostics;
    int densityTrials = 0, eliteGroups = 0, rejectedPlacements = 0, ignoredFriendly = 0;
    uint32_t sceneSeed = 0;
};
// Content + terrain -> immutable spawn instructions. No entity allocation or renderer dependencies.
// Current placement adapter treats a DS1 scene as one population room. A future DRLG
// provider will supply original room bounds, seed streams and linked warp coordinates.
PopulationPlan planPopulation(const MonsterCatalog &catalog, const LevelRecord *level,
                              const PresetRecord &preset, const Map &map, PopulationSettings settings);
void writePopulationReport(std::ostream &out, const PopulationPlan &plan, const LevelRecord *level,
                           const PresetRecord &preset, PopulationSettings settings);
} // namespace d2x
