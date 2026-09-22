#pragma once
#include "content/world_catalog.hpp"
#include "generation_seed.hpp"
#include <span>

namespace d2x {
void generateOutdoorPaths(MapRecipe &recipe, std::span<int> occupied, Seed &seed);
} // namespace d2x