#pragma once
#include "gameplay/loot/loot.hpp"
#include "world/region.hpp"
#include <string>

namespace d2x {
struct AppOptions {
    std::string mpq = "assets/mpq2";
    WorldSelection world;
    std::string screenshot, pack;
    std::string save, load;
    std::string debugPipe;
    bool debugRun = false;
    bool hidden = false, help = false, inventory = false, stash = false, maps = false, skills = false;
    int frameLimit = 0, region = -1;
    uint64_t lootSeed = LootSystem::defaultSeed;
    PopulationSettings population;
};
AppOptions parseOptions(int argc, char **argv);
} // namespace d2x
