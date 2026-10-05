#include "border_presets.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

// D2MOO DrlgOutPlace: sub_6FD80BE0/C10, PlaceAct1245OutdoorBorders,
// SetBlankBorderGridCells. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
constexpr std::array<int, 91> indices{{
    -1,1,-1,0,-1,2,-1,3,-1,0,1,9,9,-1,-1,1,8,-1,-1,12,
    -1,-1,-1,-1,12,4,-1,-1,5,2,2,10,-1,-1,-1,-1,10,1,9,9,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,11,11,3,12,-1,-1,-1,-1,12,4,4,7,-1,-1,2,10,-1,-1,-1,
    -1,10,-1,-1,6,3,-1,-1,11,11,3}};
constexpr std::array<int, 12> cliffIds{{0,16,17,0,18,19,22,0,0,23,0,0}};
int presetFor(int row, int type) {
    if (type < 0) return 0;
    if (row < 1 || row > 12) throw std::runtime_error("Invalid native perimeter lookup row");
    switch (type) {
    case 0: return cliffIds[size_t(row - 1)];
    case 1: return 3 + row;
    case 2: return 363 + row;
    case 3: return 798 + row;
    case 4: return 880 + row;
    case 5: return 956 + row;
    default: throw std::runtime_error("Unsupported native perimeter type");
    }
}
int lookup(int index) {
    if (index < 0 || index >= int(indices.size()))
        throw std::runtime_error("Native perimeter lookup exceeds table");
    return indices[size_t(index)];
}
int roadType(const LevelRecord &level, bool directed) {
    switch (level.levelType) {
    case 2: return directed ? 0 : 1;
    case 16: return 2;
    case 27: return 3;
    case 31: return level.id == 117 ? 5 : 4;
    default: return -1;
    }
}
std::pair<int, int> step(const RetailBoundaryVertex &a, const RetailBoundaryVertex &b) {
    const int dx = b.x - a.x, dy = b.y - a.y;
    if ((!dx && !dy) || (dx && dy)) throw std::runtime_error("Nonorthogonal native perimeter edge");
    return {(dx > 0) - (dx < 0), (dy > 0) - (dy < 0)};
}
int corner(int dx, int dy, int nx, int ny, int type) {
    const auto widen = [](int x) { return x < 0 ? x - 2 : x > 0 ? x + 2 : 0; };
    const int row = lookup(dy + widen(dx) + 9 * (ny + widen(nx)) + 50);
    return row == -1 ? 0 : presetFor(row, type);
}
void blankCorners(RetailOutdoorGrid &grid) {
    constexpr std::array<std::pair<int, int>, 4> steps{{{1,1},{-1,1},{1,-1},{-1,-1}}};
    for (const auto &[dx, dy] : steps) {
        const int firstX = dx > 0 ? 0 : grid.width() - 1;
        const int firstY = dy > 0 ? 0 : grid.height() - 1;
        int y = firstY;
        for (; grid.contains(firstX, y) && !(grid.cell(firstX, y).flags & 1); y += dy) {
            int x = firstX;
            for (; grid.contains(x, y) && !(grid.cell(x, y).flags & 1); x += dx)
                grid.cell(x, y).flags |= 0x100;
            if (!grid.contains(x, y)) throw std::runtime_error("Native perimeter has an unclosed blank row");
        }
        if (!grid.contains(firstX, y)) throw std::runtime_error("Native perimeter has an unclosed blank column");
    }
}
} // namespace
void placeRetailBorderPresets(const WorldCatalog &catalog, const NativeActLayout &layout, int levelId,
                             std::span<const RetailBoundaryVertex> vertices, RetailOutdoorGrid &grid) {
    const auto &level = catalog.level(levelId);
    const auto &placement = layout.levels.at(levelId);
    if (vertices.size() < 4 || grid.width() != placement.width / 8 || grid.height() != placement.height / 8)
        throw std::invalid_argument("Native perimeter dimensions do not match level");
    if (roadType(level, false) < 0) throw std::invalid_argument("Unsupported native perimeter level");
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto &a = vertices[i];
        const auto &b = vertices[(i + 1) % vertices.size()];
        const auto &c = vertices[(i + 2) % vertices.size()];
        const auto [dx, dy] = step(a, b);
        const auto [nx, ny] = step(b, c);
        const uint32_t edgeFlags = a.direction ? 3 : 1;
        if (!(a.flags & 2)) {
            const int preset = presetFor(lookup(dx + 3 * dy + 4) + 1, roadType(level, a.direction != 0));
            for (int x = a.x + dx, y = a.y + dy;; x += dx, y += dy) {
                if (preset) grid.place(catalog, preset, x, y);
                grid.cell(x, y).flags |= edgeFlags;
                if (x == b.x && y == b.y) break;
            }
        }
        if ((a.flags & 1) && !(a.flags & 2)) {
            const int distance = std::abs(dx ? b.x - a.x : b.y - a.y);
            const int x = std::min(a.x, b.x) + std::abs(dx) * distance / 2;
            const int y = std::min(a.y, b.y) + std::abs(dy) * distance / 2;
            if (level.act == 1) {
                constexpr std::array<std::pair<int, int>, 5> desert{{{373,372},{372,375},{0,0},{373,374},{374,375}}};
                const auto [first, second] = desert.at(size_t(dx + 2 * dy + 2));
                if (first) grid.place(catalog, first, x, y);
                if (second) grid.place(catalog, second, x + std::abs(dx), y + std::abs(dy));
            } else if (level.act == 0 || level.act == 3 || level.act == 4) {
                auto &cell = grid.cell(x, y);
                cell.flags = (cell.flags & ~uint32_t(0xF0000)) | (levelId == 17 ? 0x40400u : 0x30400u);
            }
        }
        const int firstScale = a.flags & 2 ? 1 : 2;
        const int nextScale = b.flags & 2 ? 1 : 2;
        int preset = corner(dx * firstScale, dy * firstScale, nx * nextScale, ny * nextScale,
                            roadType(level, a.direction || b.direction));
        if (preset == 19) preset = a.direction == 1 ? (b.direction == 1 ? 19 : 20) : 21;
        if (preset) {
            grid.place(catalog, preset, b.x, b.y);
            grid.cell(b.x, b.y).flags |= (a.direction || b.direction) ? 3 : 1;
        }
    }
    blankCorners(grid);
}
} // namespace d2x
