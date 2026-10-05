#pragma once
#include "boundary.hpp"

namespace d2x {
int retailPerimeterPreset(int dx, int dy, int type);
int retailPerimeterCorner(int dx, int dy, int nx, int ny, int type);
// Secondary substitutions, paths and room allocation follow this phase.
void placeRetailBorderPresets(const WorldCatalog &, const NativeActLayout &, int level,
                             std::span<const RetailBoundaryVertex>, RetailOutdoorGrid &);
} // namespace d2x
