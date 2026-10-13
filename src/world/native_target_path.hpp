#pragma once
#include "world/interaction_geometry.hpp"
#include <deque>

namespace d2x {
// Original unit GUID paths may be partial. Recompute from their endpoint
// using the same native policy until interaction range is reached or no path
// remains; never turn a partial path into a fabricated ground destination.
std::deque<Vec> nativeTargetPath(const Grid &grid, Vec position, const InteractionTarget &target, uint8_t targetType);
} // namespace d2x
