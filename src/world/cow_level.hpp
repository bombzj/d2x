#pragma once
#include "content/world/world_catalog.hpp"

namespace d2x {
MapRecipe generateCowLevel(Archives &archives, const WorldCatalog &catalog, uint32_t seed);
std::vector<MapRecipe> cowLevelTemplates(const WorldCatalog &catalog);
std::vector<std::string> cowLevelMissing(Archives &archives, const WorldCatalog &catalog);
void collectCowLevelResources(Archives &archives, const WorldCatalog &catalog);
} // namespace d2x