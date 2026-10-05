#pragma once
#include "world/outdoor/native_act_layout.hpp"
#include "outdoor_grid.hpp"
#include "resources/formats.hpp"
#include <functional>

namespace d2x {
using RetailPatternReader = std::function<const MapData &(const std::string &)>;
// Act I macro grid through cave/town transitions. Dirt paths and room themes
// must run before materialization; this intermediate grid is not navigable.
uint32_t initializeRetailWilderness(const WorldCatalog &, const NativeActLayout &, int level,
                                   RetailOutdoorGrid &, const RetailPatternReader &);
void finishRetailWildernessPresets(const WorldCatalog &, const NativeActLayout &, int level,
                                  RetailOutdoorGrid &);
} // namespace d2x
