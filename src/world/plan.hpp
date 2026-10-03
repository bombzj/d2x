#pragma once
#include "world/identity.hpp"
#include "content/world/world_catalog.hpp"
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct WorldSelection {
    int level = 1, preset = 0, levelType = 0, variant = 0;
    std::string map;
    uint32_t seed = 0; // Application chooses a fresh seed or restores the native map seed.
    int difficulty = 0;
};
struct RegionPlan {
    RegionDefinition definition;
    MapRecipe recipe;
};
struct WorldEntry {
    int level = 0;
    std::string name, status;
    std::vector<std::string> missing;
    std::optional<RegionId> destination;
};
struct WorldPlan {
    std::vector<RegionPlan> regions;
    std::vector<WorldEntry> entries;
    RegionId start = RegionId::Encampment;
};
WorldPlan planWorld(Archives &archives, const WorldCatalog &catalog, WorldSelection selection);
} // namespace d2x
