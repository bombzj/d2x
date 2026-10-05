#include "outdoor_paths.hpp"
#include "dirt_path_tiles.hpp"
#include "resources/formats.hpp"

// Native dirt-path mask table / floor flags adapted from D2MOO DrlgOutdoors.cpp, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
bool isOutdoorPathFloor(const MapCell &cell) {
    const auto sub = (cell.value >> 8) & 255;
    return cell.present() && cell.orientation == 0 && ((cell.value >> 20) & 63) == 0 &&
           isDirtPathSequence(sub);
}
} // namespace d2x
