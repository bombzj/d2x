#pragma once
#include "rooms.hpp"
#include "preset_scan.hpp"
#include "resources/formats.hpp"

namespace d2x {
struct RetailPresetRoomData {
    RetailRoom room;
    MapData grids;
    bool killEdgeX{}, killEdgeY{}, fillBlanks{};
    bool unitsPreloaded{};
    std::vector<RoofPopup> roofPopups; // Global tiles; shared parent preset regions.
};
// Copies the original preset's room + overlap layers. Preset unit filtering,
// popups, linked edges and warp-tile construction are handled separately.
RetailPresetRoomData buildRetailPresetRoomData(const PresetRecord &, const RetailRoom &, const MapData &);
struct RetailPresetLevel {
    NativeLevelPlacement placement;
    int preset{}, file{};
    std::vector<RetailRoom> rooms; // Creation order, not activation order.
    std::vector<RetailPresetUnit> units; // Already consumed during Scan/Pops, if enabled.
};
// InitLevelData selects a direction before InitLevel resets the level stream.
// Generation draws AllocDrlgMap's file even when overridden, then Scan/Pops
// and child-room seeds. Never chooses
// the file by server landmark matching or shares the act's placement stream.
RetailPresetLevel buildRetailPresetLevel(Archives &, const WorldCatalog &,
    const NativeActLayout &, int level);
} // namespace d2x
