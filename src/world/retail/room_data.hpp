#pragma once
#include "outdoor_layout.hpp"
#include "room_themes.hpp"
#include "tile_selector.hpp"

namespace d2x {
struct RetailOutdoorRoomData {
    RetailRoom room;
    RetailRoomGrids grids;
    struct Shadow { int x{}, y{}; uint32_t value{}; const Tile *tile{}; };
    std::vector<Shadow> shadows;
    // Retains all pointers emitted while substitution consumes the room stream.
    std::shared_ptr<RetailTileSelector> tiles;
    std::vector<RetailPresetUnit> units;
};
// Non-preset rooms only. Supply the result to RetailTileMaterializer;
// preset-room layers have their own builder. Do not supply these to Map::load.
RetailOutdoorRoomData buildRetailOutdoorRoomData(TileLibraryCache &, const WorldCatalog &,
    const LevelRecord &, const RetailRoom &, const RetailDirtPaths &, const RetailPatternReader &,
    const RetailPresetScan &, std::shared_ptr<RetailTileSelector> libraries = {});
} // namespace d2x
