#include "room_grids.hpp"
#include "world/outdoor/dirt_path_tiles.hpp"
#include <array>
#include <stdexcept>

// D2MOO InitOutdoorRoomGrids, GenerateDirtPath and grid segment rasterization.
// MIT: docs/licenses/D2MOO.txt. Dirt-road encoding/rasterization is shared with
// the existing outdoor generator; tiles and collision come from current MPQ DT1.
namespace d2x {
RetailRoomGrids initializeRetailOutdoorRoomGrids(const RetailRoom &room, const RetailDirtPaths &paths) {
    if (room.preset || room.width != 8 || room.height != 8)
        throw std::runtime_error("Native outdoor logical grids require an eight-tile non-preset room");
    RetailRoomGrids result{9, 9, std::vector<MapCell>(81), std::vector<MapCell>(81),
                           std::vector<MapCell>(81), {}};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) result.floors[size_t(y * 9 + x)].value = 0x40002;
    constexpr int stride = 11; // Native dirt path grid is room extent + three.
    std::array<uint8_t, stride * stride> raster{};
    auto sample = [&](int x, int y) -> uint8_t {
        return x >= 0 && y >= 0 && x < stride && y < stride ? raster[size_t(y * stride + x)] : 0;
    };
    auto mark = [&](int x, int y) {
        x -= room.x - 1; y -= room.y - 1;
        if (x >= 0 && y >= 0 && x < stride && y < stride) raster[size_t(y * stride + x)] = 1;
    };
    for (const auto &path : paths)
        for (size_t i = 1; i < path.size(); ++i) {
            rasterizeDirtPath(path[i - 1].x, path[i - 1].y, path[i].x, path[i].y, mark);
        }
    for (int x = 1; x <= 9; ++x)
        for (int y = 9; y >= 1; --y) {
            if (!sample(x, y)) continue;
            if (const auto floor = dirtPathFloor(dirtPathMask(x, y, sample)))
                result.floors[size_t((y - 1) * 9 + x - 1)].value = floor;
        }
    return result;
}
} // namespace d2x
