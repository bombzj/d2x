#include "room_data.hpp"

namespace d2x {
RetailOutdoorRoomData buildRetailOutdoorRoomData(TileLibraryCache &cache, const WorldCatalog &catalog,
    const LevelRecord &level, const RetailRoom &room, const RetailDirtPaths &paths,
    const RetailPatternReader &reader, const RetailPresetScan &identities,
    std::shared_ptr<RetailTileSelector> libraries) {
    RetailOutdoorRoomData result{room, initializeRetailOutdoorRoomGrids(room, paths), {},
        libraries ? std::move(libraries) :
            std::make_shared<RetailTileSelector>(cache, catalog, level.levelType, room.dt1Mask)};
    // 1.13c D2Common RVA 0x7c101 resets both seed words in InitRoomGrids.
    // Theme eligibility was already picked with the allocation stream.
    result.room.seed.random = Seed(room.seed.initial);
    applyRetailOutdoorRoomThemes(catalog, level, result.room, result.grids, reader,
        [&](int x, int y, uint32_t packed, Seed &seed) {
            const auto &tile = result.tiles->pick(13, packed, seed);
            result.shadows.push_back({x, y, packed, &tile});
        });
    for (const auto &entry : result.grids.units)
        if (auto unit = identities.resolveUnit(entry.source, entry.ds1Version,
            entry.act, room.x, room.y)) result.units.push_back(std::move(*unit));
    return result;
}
} // namespace d2x
