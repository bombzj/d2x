#pragma once
#include "region.hpp"

namespace d2x {
// Resolve native preset placeholders before looking up their Objects.txt artwork.
int resolveAct1ChestPreset(int objectClass, int levelId, uint64_t &seed);
void initializeChests(Region &region, const WorldCatalog &catalog, const Table &objects);
} // namespace d2x
