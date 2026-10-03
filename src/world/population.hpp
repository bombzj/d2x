#pragma once
#include "content/monsters/monster_catalog.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include "gameplay/monsters/population_settings.hpp"
#include "map.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace d2x {
struct PopulationPlan {
    std::vector<MonsterSpawn> spawns;
    std::vector<std::string> roster, diagnostics;
    int densityTrials = 0, eliteGroups = 0, rejectedPlacements = 0, ignoredFriendly = 0;
    uint32_t sceneSeed = 0;
};
// Content + terrain -> immutable spawn instructions. No entity allocation or renderer dependencies.
// Generated maps supply room footprints and linked warp arrivals. Complete presets
// retain the scene adapter; the original room allocation/stream scheduling is not reproduced byte for byte.
PopulationPlan planPopulation(const MonsterCatalog &catalog, const LevelRecord *level,
                              const PresetRecord &preset, const Map &map, PopulationSettings settings);
void writePopulationReport(std::ostream &out, const PopulationPlan &plan, const LevelRecord *level,
                           const PresetRecord &preset, PopulationSettings settings);
} // namespace d2x
