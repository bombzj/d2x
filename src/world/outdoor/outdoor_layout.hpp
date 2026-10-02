#pragma once
#include "content/world/world_catalog.hpp"

namespace d2x {
struct OutdoorPosition {
    int level = 0, x = 0, y = 0, width = 0, height = 0, direction = 0;
    std::vector<MapRecipe::Boundary> boundaries;
    uint32_t flags = 0;
};
// Coordinates and attachments are expressed in native tiles, never screen pixels.
std::map<int, OutdoorPosition> layoutAct1(const WorldCatalog &catalog, uint32_t seed);
std::map<int, OutdoorPosition> layoutAct2(const WorldCatalog &catalog, uint32_t seed);
std::map<int, OutdoorPosition> layoutAct4(const WorldCatalog &catalog, uint32_t seed);
} // namespace d2x
