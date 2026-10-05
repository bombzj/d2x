#pragma once
#include "boundary.hpp"

namespace d2x {
// Secondary substitutions, paths and room allocation follow this phase.
void placeRetailBorderPresets(const WorldCatalog &, const NativeActLayout &, int level,
                             std::span<const RetailBoundaryVertex>, RetailOutdoorGrid &);
} // namespace d2x
