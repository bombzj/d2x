#pragma once
#include "world/interaction_geometry.hpp"
#include <deque>

namespace d2x {
// Retail sub_6FD5DB70 retains an active path until a player/monster target
// moves more than five integer subtiles on either axis from its planned goal.
bool nativeUnitTargetMoved(Vec planned, Vec current, uint8_t targetType);
// Original unit GUID paths may be partial. Recompute from their endpoint
// using the same native policy until interaction range is reached or no path
// remains; never turn a partial path into a fabricated ground destination.
std::deque<Vec> nativeTargetPath(const Grid &grid, Vec position, const InteractionTarget &target, uint8_t targetType);
} // namespace d2x
