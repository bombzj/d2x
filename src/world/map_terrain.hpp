#pragma once
#include "resources/formats.hpp"
#include "content/world/world_catalog.hpp"
#include <algorithm>
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
    struct TileAnimation { std::vector<int> frames; int speed = 80; };
    std::map<std::pair<size_t, uint32_t>, TileAnimation> animations;
    // Prepared room instances preserve overlapping shadows, explicit corner
    // halves and each room's DT1 choice. They never re-enter DS1 tile selection.
    struct Instance {
        int x{}, y{}, type{}, tile{};
        uint32_t flags{};
        std::vector<int> frames{};
        int speed{};
        size_t room{};
        bool wallArray{};
        int renderTile(float seconds) const {
            if (frames.empty()) return tile;
            const auto ticks = uint64_t(std::max(0.f, seconds) * 25.f);
            return frames[(ticks * uint64_t(speed) / 256) % frames.size()];
        }
    };
    bool preparedRooms{};
    std::vector<Instance> instances;
    struct PreparedRoom {
        int level{}, x{}, y{}, width{}, height{}, preset{}, file{}, parentX{}, parentY{};
        std::vector<size_t> near;
        uint32_t flags{};
    };
    std::vector<PreparedRoom> rooms;
    // Native client decorations (river graphics/sound anchors) do not become
    // authoritative actors or acquire fabricated server unit IDs.
    std::vector<MapObject> clientObjects;
    struct Exit {
        int slot{}, destination{}; WarpRecord selection; Vec position;
        std::vector<size_t> visible{}, lit{};
    };
    std::vector<Exit> exits;
    std::string name, path;
    int unresolved = 0;
    int tileIndex(const MapCell &cell, int x, int y) const {
        const auto found = tileChoices.find({x, y, cell.libraryScope, cell.key()});
        return found == tileChoices.end() ? -1 : found->second;
    }
    int renderTileIndex(const MapCell &cell, int x, int y, float seconds) const {
        const auto animation = animations.find({cell.libraryScope, cell.key()});
        if (animation == animations.end()) return tileIndex(cell, x, y);
        const auto &value = animation->second;
        const auto ticks = uint64_t(std::max(0.f, seconds) * 25.f);
        return value.frames[(ticks * uint64_t(value.speed) / 256) % value.frames.size()];
    }
};
} // namespace d2x
