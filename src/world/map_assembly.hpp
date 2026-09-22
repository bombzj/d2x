#pragma once
#include "content/world_catalog.hpp"
#include "resources/formats.hpp"

namespace d2x {
MapData assembleMap(Archives &archives, const MapRecipe &recipe);
} // namespace d2x
