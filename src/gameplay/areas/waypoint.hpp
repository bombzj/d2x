#pragma once
#include "core/math.hpp"
#include <cmath>

namespace d2x {
// D2Common WAYPOINTS: seven 16-bit flag words after the 0x102 header.
inline constexpr int nativeWaypointCount = 7 * 16;
constexpr bool nativeWaypointIndex(int index) { return index >= 0 && index < nativeWaypointCount; }
// DRLGWARP_GetWaypointRoomExFromLevel returns game tiles; Dungeon then
// converts to subtiles and adds the original +3 centre offset.
inline Vec waypointSpawnAnchor(Vec preset) {
    return {std::floor(preset.x / 5.f) * 5.f + 3.f, std::floor(preset.y / 5.f) * 5.f + 3.f};
}
}
