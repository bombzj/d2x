#pragma once
#include "room_grids.hpp"

namespace d2x {
using RetailPatternReader = std::function<const MapData &(const std::string &)>;
// Shadow tile selection happens immediately during a substitution stamp and
// consumes this room's stream before the next group/position draw. The caller
// must retain every emitted shadow, including overlapping entries.
using RetailShadowEmitter = std::function<void(int x, int y, uint32_t value, Seed &)>;
void applyRetailOutdoorRoomThemes(const WorldCatalog &, const LevelRecord &,
    RetailRoom &, RetailRoomGrids &, const RetailPatternReader &, const RetailShadowEmitter &);
} // namespace d2x
