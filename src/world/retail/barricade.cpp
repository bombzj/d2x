#include "barricade.hpp"
#include "border_presets.hpp"
#include "secondary_border.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

// D2MOO DrlgOutSiege: eight-tile grid, sixteen-tile perimeter, substitutions,
// prison probes and ordered special placement. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
int substitutionPreset(uint32_t wall, bool snow) {
    const int style = int((wall >> 20) & 63), sequence = int((wall >> 8) & 255);
    if (style == 49 && ((sequence >= 1 && sequence <= 16) || (sequence >= 31 && sequence <= 46)))
        return (snow ? 987 : 915) + sequence - (sequence >= 31 ? 31 : 1);
    const int cliff = snow ? 957 : 881, ravine = snow ? 969 : 893;
    if (style == 48) {
        if (sequence == 1) return cliff + 2;
        if (sequence >= 2 && sequence <= 3) return cliff + sequence - 2;
        if (sequence == 4) return cliff + 3;
        if (sequence == 5) return ravine + 2;
        if (sequence >= 6 && sequence <= 7) return ravine + sequence - 6;
        if (sequence == 8) return ravine + 3;
        if (sequence == 30) return 0;
        if (sequence == 31) return -5;
    }
    throw std::runtime_error("Unsupported native barricade substitution code");
}
void prisons(const WorldCatalog &catalog, RetailOutdoorGrid &grid) {
    int count = 0;
    auto place = [&](int x, int y) {
        const auto &cell = grid.cell(x, y);
        if (!(cell.flags & 0x200) || cell.preset < 915 || cell.preset > 922) return;
        grid.place(catalog, cell.preset + 16, x, y);
        ++count;
    };
    for (int attempt = 0; attempt < 90 && count < 3; ++attempt) {
        const int x = 2 * grid.random().below(grid.width() / 2);
        const int y = 2 * grid.random().below(grid.height() / 2);
        place(x, y);
    }
    const int ox = 2 * grid.random().below(grid.width() / 2);
    const int oy = 2 * grid.random().below(grid.height() / 2);
    for (int y = 0; y < grid.height() && count < 3; ++y)
        for (int x = 0; x < grid.width() && count < 3; ++x)
            place((x + ox) % grid.width(), (y + oy) % grid.height());
    // Original warns when insufficient prison tiles exist; it does not invent tiles.
}
void special(const WorldCatalog &catalog, int level, RetailOutdoorGrid &grid) {
    struct Placement { int level, north, west, file, count; bool required; };
    constexpr Placement list[]{
        {111,955,956,0,1,true}, {112,955,956,0,1,true}, {117,955,956,1,1,true},
        {112,953,953,-1,1,true}, {117,954,954,-1,1,true},
        {111,944,947,-1,1,false}, {111,942,945,-1,4,false}, {111,943,946,-1,4,false},
        {112,941,941,-1,1,false}, {112,939,939,-1,1,false}, {112,940,940,-1,5,false},
        {117,948,948,-1,4,false}, {117,949,949,-1,4,false}, {117,950,950,-1,4,false},
        {117,951,951,-1,3,false}};
    for (const auto &p : list) {
        if (p.level != level) continue;
        bool added = false;
        for (int i = 0; i < p.count; ++i)
            added = grid.placeRandom(catalog, grid.width() < grid.height() ? p.north : p.west, p.file);
        if (!added && p.required) throw std::runtime_error("Native barricade required preset cannot fit");
    }
}
}
uint32_t initializeRetailBarricade(const WorldCatalog &catalog, const NativeActLayout &layout,
    int level, RetailOutdoorGrid &grid, const RetailPatternReader &reader) {
    const auto flags = layout.levels.at(level).outdoorFlags;
    if (level == 110) {
        const int size = catalog.presets().at(865).width / 8;
        int x = grid.width() - size;
        for (int i = 0; i < 15; ++i, x -= size) grid.place(catalog, 865 + i, x, 0, 0);
        return flags;
    }
    if (level != 111 && level != 112 && level != 117) throw std::invalid_argument("Not a native barricade level");
    const bool snow = level == 117;
    const int type = snow ? 5 : 4;
    const auto vertices = buildRetailBoundary(layout, level);
    markRetailBoundaryLinks(layout, level, vertices, grid);
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto &a = vertices[i], &b = vertices[(i + 1) % vertices.size()],
                   &c = vertices[(i + 2) % vertices.size()];
        const int dx = (b.x > a.x) - (b.x < a.x), dy = (b.y > a.y) - (b.y < a.y);
        const int nx = (c.x > b.x) - (c.x < b.x), ny = (c.y > b.y) - (c.y < b.y);
        int x = a.x & ~1, y = a.y & ~1;
        const int bx = b.x & ~1, by = b.y & ~1;
        const int border = retailPerimeterPreset(dx, dy, type);
        if (!(a.flags & 2)) while (x != bx || y != by) {
            x += 2 * dx; y += 2 * dy;
            grid.place(catalog, border, x, y);
            grid.cell(x, y).flags |= 1;
        }
        if (a.flags & 1) {
            const int lx = (std::max(a.x, b.x) - 4 * std::abs(dx)) & ~1;
            const int ly = (std::max(a.y, b.y) - 4 * std::abs(dy)) & ~1;
            grid.cell(lx, ly).flags |= 0x400;
            grid.cell(lx + 2 * std::abs(dx), ly + 2 * std::abs(dy)).flags |= 0x400;
        }
        if (const int corner = retailPerimeterCorner(2 * dx, 2 * dy, 2 * nx, 2 * ny, type)) {
            grid.place(catalog, corner, bx, by);
            grid.cell(bx, by).flags |= 1;
        }
    }
    if (level == 111) {
        grid.cell(grid.width() - 2, grid.height() - 4).flags |= 0x400;
        grid.cell(grid.width() - 2, grid.height() - 3).flags |= 0x400;
    }
    constexpr std::array<int, 12> dx{-1,0,1,0,0,1,0,-1,-1,0,1,0};
    constexpr std::array<int, 12> dy{0,-1,0,1,-1,0,1,0,0,-1,0,1};
    const int cliff = snow ? 957 : 881, ravine = snow ? 969 : 893;
    int x = grid.width() - 2, y = 0;
    for (int count = 0; x != 0 || y != grid.height() - 2; ++count) {
        if (count > grid.width() * grid.height()) throw std::runtime_error("Native ravine perimeter does not close");
        const int index = grid.cell(x, y).preset - cliff;
        if (index < 0 || index >= int(dx.size())) throw std::runtime_error("Native ravine has an invalid cliff index");
        grid.place(catalog, ravine + index, x, y);
        x += 2 * dx[size_t(index)]; y += 2 * dy[size_t(index)];
    }
    grid.place(catalog, snow ? 982 : 906, grid.width() - 2, 0);
    grid.place(catalog, snow ? 981 : 905, 0, grid.height() - 2);
    for (int side = 0; side < 4; ++side) {
        const bool vertical = side >= 2;
        const int limit = vertical ? grid.height() : grid.width();
        for (int i = 0; i < limit; ++i) {
            const int cx = vertical ? (side == 2 ? 0 : grid.width() - 2) : i;
            const int cy = vertical ? i : (side == 0 ? 0 : grid.height() - 2);
            if (!(grid.cell(cx, cy).flags & 0x400)) continue;
            constexpr int gates[]{909,908,910,907};
            grid.place(catalog, gates[side], cx, cy);
            break;
        }
    }
    const bool vertical = grid.width() <= grid.height();
    if (level == 117) grid.place(catalog, vertical ? 983 : 984,
        vertical ? 2 : grid.width() - 2, vertical ? grid.height() - 2 : 2, 0);
    if (level == 112 || level == 117) grid.place(catalog,
        snow ? (vertical ? 985 : 986) : (vertical ? 913 : 914), vertical ? 2 : 0, vertical ? 0 : 2, 0);
    if (level == 111) {
        const auto &transition = catalog.presets().at(880);
        const int cx = grid.width() - transition.width / 8, cy = grid.height() - transition.height / 8;
        grid.place(catalog, 880, cx, cy);
        grid.place(catalog, 896, cx, cy - 2);
    }
    for (const auto &record : catalog.substitutions()) if (record.type == 12)
        replaceRetailSecondaryBorder(catalog, record, reader(record.file), level, flags, 0, grid,
            [snow](uint32_t wall) { return substitutionPreset(wall, snow); });
    if (level == 111) prisons(catalog, grid);
    special(catalog, level, grid);
    return flags;
}
} // namespace d2x
