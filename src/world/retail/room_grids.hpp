#pragma once
#include "rooms.hpp"
#include "dirt_paths.hpp"
#include "resources/formats.hpp"

namespace d2x {
// Raw native logical grids before substitution or DT1 selection. Includes the
// room's one-tile overlap; not an assembled level or a navigation grid.
struct RetailRoomGrids {
    int width{}, height{};
    std::vector<MapCell> floors, walls, shadows;
    struct AuthoredUnit { MapObject source; int ds1Version{}, act{}; };
    // Raw DS1 identity retained for server-landmark comparison, not local actors.
    std::vector<AuthoredUnit> units;
};
RetailRoomGrids initializeRetailOutdoorRoomGrids(const RetailRoom &, const RetailDirtPaths &);
} // namespace d2x
