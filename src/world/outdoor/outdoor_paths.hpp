#pragma once
#include "content/world/world_catalog.hpp"
#include "world/generation_seed.hpp"
#include <span>

namespace d2x {
struct MapCell;
bool isOutdoorPathFloor(const MapCell &cell);
void generateOutdoorPaths(Archives &archives, MapRecipe &recipe, std::span<int> occupied, Seed &seed);
} // namespace d2x
