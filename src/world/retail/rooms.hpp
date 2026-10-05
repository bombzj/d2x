#pragma once
#include "world/outdoor/native_act_layout.hpp"
#include "outdoor_grid.hpp"
#include <functional>

namespace d2x {
struct RetailRoom {
    int x{}, y{}, width{}, height{};
    int preset{}, file{}, mapX{}, mapY{};
    uint32_t flags{}, outdoorFlags{}, auxiliary{}, dt1Mask{}, themeMask{};
    RetailRoomSeed seed;
};
// Preset scanning must consume the level stream before room allocation and
// return the native per-eight-tile-cell flags (including exit/waypoint scans).
using RetailPresetScanner = std::function<std::vector<uint32_t>(
    const PresetRecord &, int file, int x, int y, int width, int height, uint32_t flags, Seed &)>;
// Creation order is row-major. Native level-list activation order is reversed.
// This allocates descriptors only; it does not certify tiles or navigation.
std::vector<RetailRoom> allocateRetailOutdoorRooms(const WorldCatalog &,
    const NativeActLayout &, int level, RetailOutdoorGrid &, const RetailPresetScanner &);
// Shared BuildArea partition for outdoor presets, preset levels and converted
// maze rooms. File selection/forced overrides precede this function; Scan/Pops
// consume the level stream before any child room is allocated.
std::vector<RetailRoom> allocateRetailPresetRooms(const PresetRecord &, int file,
    int x, int y, int width, int height, uint32_t flags, Seed &levelRandom,
    const RetailPresetScanner &, bool singleRoom = false);
} // namespace d2x
