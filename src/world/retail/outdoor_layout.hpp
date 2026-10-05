#pragma once
#include "dirt_paths.hpp"
#include "rooms.hpp"
#include "preset_scan.hpp"
#include "resources/archive.hpp"

namespace d2x {
struct RetailOutdoorLayout {
    NativeLevelPlacement placement;
    RetailOutdoorGrid grid;
    RetailDirtPaths paths;
    uint32_t flags{};
    std::vector<RetailRoom> rooms;
    std::map<RetailPresetKey, std::vector<RetailPresetUnit>> presetUnits;
};
// Reconstructs Act I macro placement and room allocation from current MPQ.
// This value cannot be passed to Map::load: logical grids, room themes and tile
// materialization are later stages, never supplied by offline DRLG.
RetailOutdoorLayout buildRetailOutdoorLayout(Archives &, const WorldCatalog &,
                                            const NativeActLayout &, int level);
} // namespace d2x
