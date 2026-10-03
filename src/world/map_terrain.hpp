#pragma once
#include "resources/formats.hpp"
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace d2x {
// Decoded DS1/DT1 and chosen variants. Shared DT1 libraries own the tile pointers.
// Grid, object obstacles, room activation and arrivals belong to the live Map.
// The authority may edit/restore walls for the existing staff opening operation.
struct MapTerrain {
    MapData data;
    std::vector<std::shared_ptr<const std::vector<Tile>>> libraries;
    std::vector<const Tile *> tiles;
    std::map<uint32_t, std::vector<int>> lookup;
    std::vector<std::map<uint32_t, std::vector<int>>> scopedLookup;
    // Chosen once at load: collision, automap and rendering read the same DT1 variant.
    std::map<std::tuple<int, int, size_t, uint32_t>, int> tileChoices;
    std::string name, path;
    int unresolved = 0;
    int tileIndex(const MapCell &cell, int x, int y) const {
        const auto found = tileChoices.find({x, y, cell.libraryScope, cell.key()});
        return found == tileChoices.end() ? -1 : found->second;
    }
};
} // namespace d2x
