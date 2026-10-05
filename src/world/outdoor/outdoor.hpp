#pragma once
#include "content/world/world_catalog.hpp"
namespace d2x {
std::vector<MapRecipe> outdoorTemplates(const WorldCatalog &catalog);
std::vector<std::string> outdoorMissing(Archives &archives, const WorldCatalog &catalog);
} // namespace d2x
