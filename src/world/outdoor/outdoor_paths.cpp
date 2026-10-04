#include "outdoor_paths.hpp"
#include "world/navigation.hpp"
#include "world/map_assembly.hpp"
#include <algorithm>
#include <array>

// Native dirt-path mask table / floor flags adapted from D2MOO DrlgOutdoors.cpp, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
constexpr std::array<uint8_t, 256> pathTiles{
    0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x0E, 0x0E, 0x06, 0x13, 0x0E, 0x0E, 0x06, 0x13,
    0x0F, 0x0F, 0x05, 0x05, 0x0F, 0x0F, 0x15, 0x15, 0x08, 0x08, 0x0A, 0x26, 0x08, 0x08, 0x28, 0x14,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x0E, 0x0E, 0x06, 0x13, 0x0E, 0x0E, 0x06, 0x13,
    0x0F, 0x0F, 0x05, 0x05, 0x0F, 0x0F, 0x15, 0x15, 0x08, 0x08, 0x0A, 0x26, 0x08, 0x08, 0x28, 0x14,
    0x0D, 0x0D, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x07, 0x04, 0x04, 0x0B, 0x25, 0x04, 0x04, 0x0B, 0x2B,
    0x03, 0x03, 0x0C, 0x0C, 0x03, 0x03, 0x27, 0x27, 0x09, 0x09, 0x02, 0x2B, 0x09, 0x09, 0x2C, 0x1A,
    0x0D, 0x0D, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x07, 0x17, 0x17, 0x29, 0x11, 0x17, 0x17, 0x29, 0x11,
    0x03, 0x03, 0x0C, 0x0C, 0x03, 0x03, 0x27, 0x27, 0x2A, 0x2A, 0x2E, 0x2A, 0x2A, 0x2A, 0x21, 0x1F,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x0E, 0x0E, 0x06, 0x13, 0x0E, 0x0E, 0x06, 0x13,
    0x0F, 0x0F, 0x05, 0x05, 0x0F, 0x0F, 0x15, 0x15, 0x08, 0x08, 0x0A, 0x26, 0x08, 0x08, 0x23, 0x14,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x0E, 0x0E, 0x06, 0x13, 0x0E, 0x0E, 0x06, 0x13,
    0x0F, 0x0F, 0x05, 0x05, 0x0F, 0x0F, 0x15, 0x15, 0x08, 0x08, 0x0A, 0x26, 0x08, 0x08, 0x28, 0x14,
    0x0D, 0x0D, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x07, 0x04, 0x04, 0x0B, 0x25, 0x04, 0x04, 0x0B, 0x25,
    0x12, 0x12, 0x23, 0x23, 0x12, 0x12, 0x16, 0x16, 0x24, 0x24, 0x2D, 0x22, 0x24, 0x24, 0x1C, 0x1D,
    0x0D, 0x0D, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x07, 0x17, 0x17, 0x29, 0x11, 0x17, 0x17, 0x29, 0x11,
    0x12, 0x12, 0x23, 0x23, 0x12, 0x12, 0x16, 0x16, 0x18, 0x18, 0x19, 0x20, 0x18, 0x18, 0x1E, 0x01};
struct Endpoint {
    Vec position, approach;
};
} // namespace
bool isOutdoorPathFloor(const MapCell &cell) {
    const auto sub = (cell.value >> 8) & 255;
    return cell.present() && cell.orientation == 0 && ((cell.value >> 20) & 63) == 0 &&
           sub != 0 && std::find(pathTiles.begin(), pathTiles.end(), sub) != pathTiles.end();
}
void generateOutdoorPaths(Archives &archives, MapRecipe &recipe, std::span<int> occupied, Seed &seed) {
    int width = recipe.width / 8, height = recipe.height / 8;
    if (occupied.size() != size_t(width) * height)
        throw std::runtime_error("Invalid outdoor path grid");
    Grid grid(width, height);
    for (size_t index = 0; index < occupied.size(); ++index)
        grid.blocked[index] = occupied[index] != 0;
    std::vector<Endpoint> endpoints;
    auto inOpening = [&](int x, int y) {
        return std::any_of(recipe.boundaries.begin(), recipe.boundaries.end(), [&](const auto &boundary) {
            const int lateral = boundary.side % 2 ? y : x;
            const int normal = boundary.side % 2 ? x : y;
            const int extent = boundary.side % 2 ? recipe.width : recipe.height;
            return lateral >= boundary.start && lateral < boundary.end &&
                (boundary.side == 1 || boundary.side == 2 ? normal < 8 : normal >= extent - 8);
        });
    };
    for (const auto &boundary : recipe.boundaries) {
        float lateral = boundary.pathStart >= 0 ? float(boundary.pathStart) : boundary.start + 3.f;
        Vec position{boundary.side == 1   ? 0.f
                     : boundary.side == 3 ? recipe.width - 1.f
                                          : lateral,
                     boundary.side == 2   ? 0.f
                     : boundary.side == 0 ? recipe.height - 1.f
                                          : lateral};
        constexpr int inwardX[]{0, 1, 0, -1}, inwardY[]{-1, 0, 1, 0};
        endpoints.push_back(
            {position, position + Vec{inwardX[boundary.side] * 8.f, inwardY[boundary.side] * 8.f}});
    }
    std::optional<Vec> bridge;
    for (const auto &piece : recipe.pieces) {
        Vec position{piece.x + 3.f, piece.y + 3.f};
        if (piece.preset == 51 || piece.preset == 52)
            endpoints.push_back({position, position + (piece.variant ? Vec{0, 8} : Vec{8, 0})});
        if (piece.preset == 24 || piece.preset == 25)
            endpoints.push_back({position, position + (piece.preset == 24 ? Vec{0, 8} : Vec{8, 0})});
        if (piece.preset == 28 && piece.variant == 1)
            bridge = position;
    }
    if (endpoints.empty())
        return;
    Vec center{};
    for (const auto &endpoint : endpoints)
        center = center + endpoint.position;
    center = grid.nearest(center * (1.f / (8 * endpoints.size()))) * 8 - Vec{1, 1};
    // One-cell halo keeps the native 3x3 road mask continuous across level borders.
    const int stride = recipe.width + 2;
    Bytes path(size_t(stride) * (recipe.height + 2));
    auto pathIndex = [&](int x, int y) { return size_t(y + 1) * stride + x + 1; };
    const auto authored = assembleMap(archives, recipe);
    for (int y = 0; y < recipe.height; ++y)
        for (int x = 0; x < recipe.width; ++x)
            for (const auto &layer : authored.floors)
                if (isOutdoorPathFloor(layer[size_t(y) * authored.width + x]))
                    path[pathIndex(x, y)] = 1;
    for (const auto &boundary : recipe.boundaries) {
        const int start = boundary.pathStart >= 0 ? boundary.pathStart : boundary.start + 3;
        const int end = boundary.pathEnd >= 0 ? boundary.pathEnd : std::min(start + 2, boundary.end);
        for (int lateral = start; lateral < end; ++lateral) {
            const int x = boundary.side == 1 ? -1 : boundary.side == 3 ? recipe.width : lateral;
            const int y = boundary.side == 2 ? -1 : boundary.side == 0 ? recipe.height : lateral;
            if (x >= -1 && y >= -1 && x <= recipe.width && y <= recipe.height)
                path[pathIndex(x, y)] = 1;
        }
    }
    auto mark = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= recipe.width || y >= recipe.height)
            return;
        auto index = size_t(y / 8) * width + x / 8;
        if (occupied[index] == 0 || occupied[index] == -2 ||
            (occupied[index] == -1 && inOpening(x, y))) {
            // Keep the reserved boundary macrocell: permitting this road strip
            // must not allow another path to paint beside the actual opening.
            if (occupied[index] != -1) occupied[index] = -2;
            path[pathIndex(x, y)] = 1;
        }
    };
    auto rasterize = [&](Vec from, Vec to) {
        int x = int(from.x), y = int(from.y);
        int deltaX = std::abs(int(to.x) - x), deltaY = std::abs(int(to.y) - y);
        int stepX = to.x >= x ? 1 : -1, stepY = to.y >= y ? 1 : -1, error = 0;
        bool horizontal = deltaX >= deltaY;
        int length = horizontal ? deltaX : deltaY;
        for (int step = 0; step <= length; ++step) {
            mark(x, y);
            mark(x + !horizontal, y + horizontal);
            if (horizontal) {
                x += stepX;
                error += deltaY;
                if (error > deltaX) {
                    y += stepY;
                    error -= deltaX;
                }
            } else {
                y += stepY;
                error += deltaX;
                if (error > deltaY) {
                    x += stepX;
                    error -= deltaY;
                }
            }
        }
    };
    for (const auto &endpoint : endpoints) {
        Vec destination = center;
        if (bridge)
            destination = *bridge + (endpoint.position.x > bridge->x ? Vec{16, 0} : Vec{-8, 0});
        Vec from = grid.nearest(endpoint.approach * .125f);
        Vec to = grid.nearest(destination * .125f);
        auto route = grid.path(from, to);
        if (route.empty())
            continue;
        Vec previous = endpoint.position;
        rasterize(previous, from * 8 - Vec{1, 1});
        previous = from * 8 - Vec{1, 1};
        int direction = seed.next() & 3;
        constexpr int offsetX[]{1, 0, -1, 0}, offsetY[]{0, 1, 0, -1};
        for (size_t index = 0; index < route.size(); ++index) {
            Vec next = route[index] * 8 - Vec{1, 1};
            if (index + 1 < route.size()) {
                next = next + Vec{float((2 + int(seed.next() & 1)) * offsetX[direction]),
                                  float((2 + int(seed.next() & 1)) * offsetY[direction])};
                direction = (direction + 1) % 4;
            }
            rasterize(previous, next);
            previous = next;
        }
    }
    auto present = [&](int x, int y) {
        return x >= -1 && y >= -1 && x <= recipe.width && y <= recipe.height && path[pathIndex(x, y)];
    };
    for (int y = 0; y < recipe.height; ++y)
        for (int x = 0; x < recipe.width; ++x) {
            if (!present(x, y))
                continue;
            unsigned mask = 0;
            for (int index = 8; index >= 0; --index)
                if (index != 4)
                    mask = (mask << 1) | unsigned(present(x + index / 3 - 1, y + 1 - index % 3));
            if (pathTiles[mask])
                recipe.floorOverrides.push_back({x, y, (uint32_t(pathTiles[mask]) << 8) | 0x82});
        }
}
} // namespace d2x
