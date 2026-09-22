#pragma once
#include "world_catalog.hpp"
#include <ostream>

namespace d2x {
// Read-only resource inventory, also useful before installing a larger MPQ set.
void writeWorldReport(std::ostream &out, Archives &archives, const WorldCatalog &catalog, int level = 0);
} // namespace d2x
