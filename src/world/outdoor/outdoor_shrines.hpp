#pragma once
#include "content/world/world_catalog.hpp"
#include "world/generation_seed.hpp"
#include <span>

namespace d2x {
void placeAct1OutdoorShrines(Archives &archives, const WorldCatalog &catalog, const LevelRecord &level,
                             std::span<int> occupied, MapRecipe &recipe, Seed &seed);
} // namespace d2x
