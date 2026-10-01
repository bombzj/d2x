#pragma once
#include "outdoor_layout.hpp"
namespace d2x {
std::map<int, MapRecipe> generateAct1Outdoors(Archives &archives, const WorldCatalog &catalog, uint32_t seed);
std::vector<MapRecipe> outdoorTemplates(const WorldCatalog &catalog);
std::vector<std::string> outdoorMissing(Archives &archives, const WorldCatalog &catalog);
} // namespace d2x
