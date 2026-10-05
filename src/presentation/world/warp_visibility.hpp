#pragma once
#include "world/map_terrain.hpp"
#include <optional>

namespace d2x {
// D2MOO DRLGWARP_UpdateWarpRoomSelect/Deselect: display-only chain toggles.
inline std::vector<int8_t> warpTileVisibility(const MapTerrain &terrain, std::optional<size_t> selected) {
    std::vector<int8_t> result(terrain.instances.size(), -1);
    if (!selected) return result;
    const auto &exit = terrain.exits.at(*selected);
    if (exit.lit.empty()) return result;
    for (const auto tile : exit.visible) result.at(tile) = 1;
    for (const auto tile : exit.lit) result.at(tile) = 0;
    return result;
}
} // namespace d2x
