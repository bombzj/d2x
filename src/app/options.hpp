#pragma once
#include "gameplay/monsters/population_settings.hpp"
#include "gameplay/loot/loot.hpp"
#include "world/region.hpp"
#include <optional>
#include <string>

namespace d2x {
struct AppOptions {
    std::string mpq = "assets/mpq2";
    WorldSelection world;
    std::string screenshot, pack;
    std::string save, load;
    std::string characterClass;
    std::string debugPipe;
    bool debugRun = false;
    bool directGame = false;
    bool hidden = false, help = false, inventory = false, stash = false, skills = false;
    int frameLimit = 0, region = -1;
    std::optional<uint32_t> seed;
    bool mapSeedExplicit = false, populationSeedExplicit = false;
    PopulationSettings population;
};
AppOptions parseOptions(int argc, char **argv);
} // namespace d2x
