#include "wilderness.hpp"
#include "border_presets.hpp"
#include "secondary_border.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

// D2MOO DrlgOutWild and DrlgOutdoors: Act I initialization, rivers, caves,
// special presets, waypoint/shrine reservation. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
void secondary(const WorldCatalog &catalog, int level, uint32_t flags, int type,
               RetailOutdoorGrid &grid, const RetailPatternReader &reader) {
    bool found = false;
    for (const auto &record : catalog.substitutions())
        if (record.type == type) {
            found = true;
            replaceRetailSecondaryBorder(catalog, record, reader(record.file), level, flags, 4, grid);
        }
    if (!found) throw std::runtime_error("Native wilderness border substitution is missing");
}
bool riverFits(const RetailOutdoorGrid &grid, int x) {
    for (int y = 0; y < grid.height(); ++y)
        if ((grid.cell(x, y).flags | grid.cell(x + 1, y).flags) & 2) return false;
    return true;
}
void river(const WorldCatalog &catalog, RetailOutdoorGrid &grid, uint32_t flags, int x) {
    constexpr std::array<std::pair<int, int>, 12> files{{
        {2,2},{0,3},{1,1},{3,0},{0,2},{0,1},{1,0},{2,0},{2,3},{1,3},{3,1},{3,2}}};
    for (int y = 0; y < grid.height(); ++y)
        for (int bank = 0; bank < 2; ++bank) {
            const auto cell = grid.cell(x + bank, y);
            int file = cell.flags & 0x100 ? 0 : 3;
            if (cell.preset) {
                if (cell.preset < 4 || cell.preset > 15)
                    throw std::runtime_error("Native river overlaps an unsupported preset");
                if (cell.preset == 7 && ((cell.flags >> 16) & 15) == 3) file = 3;
                else {
                    const auto pair = files[size_t(cell.preset - 4)];
                    file = bank ? pair.second : pair.first;
                }
            }
            grid.place(catalog, 26 + bank, x + bank, y, file);
        }
    if (!(flags & 20)) return;
    const int count = grid.height() - 2, start = grid.random().below(count);
    for (int i = 0; i < count; ++i) {
        const int y = (start + i) % count + 1;
        if (!grid.canPlace(catalog, 0, x - 1, y) ||
            (!(flags & 4) && !grid.canPlace(catalog, 0, x + 2, y))) continue;
        if (((grid.cell(x, y).flags >> 16) & 15) != 3 ||
            ((grid.cell(x + 1, y).flags >> 16) & 15) != 3) continue;
        grid.place(catalog, 28, x, y, 1);
        grid.place(catalog, 28, x + 1, y, flags & 4 ? 3 : 2);
        return;
    }
    // Native placement can fail without retrying or consuming another seed.
}
void waypoint(const WorldCatalog &catalog, const NativeActLayout &layout, int level,
              RetailOutdoorGrid &grid) {
    if (level == 3) {
        const auto &visible = layout.connections.at(level).visible;
        uint32_t bit = 0;
        for (size_t i = 0; i < visible.size(); ++i) if (visible[i] == 2) { bit = 1u << (i + 4); break; }
        for (int y = 0; y < grid.height(); ++y)
            for (int x = 0; x < grid.width(); ++x)
                if ((grid.cell(x, y).links & bit) && (grid.cell(x, y).flags & 0x400)) {
                    auto &cell = grid.cell(std::clamp(x, 1, grid.width() - 2),
                                           std::clamp(y, 1, grid.height() - 2));
                    cell.links |= 0x20000;
                    cell.flags |= 0x800;
                    return;
                }
    }
    for (const auto &[x, y] : grid.shuffledInterior())
        if (grid.canPlace(catalog, 0, x, y)) {
            grid.cell(x, y).links |= 0x10000;
            grid.cell(x, y).flags |= 0x800;
            return;
        }
}
void shrines(const WorldCatalog &catalog, RetailOutdoorGrid &grid) {
    int type = int(grid.random().next() & 3), count = 5;
    for (const auto &[x, y] : grid.shuffledInterior()) {
        if (!count) break;
        if (grid.canPlace(catalog, 0, x, y)) {
            grid.cell(x, y).links |= 0x1000u << type;
            grid.cell(x, y).flags |= 0x1000;
            type = (type + 1) & 3;
            --count;
        }
    }
}
void special(const WorldCatalog &catalog, int level, RetailOutdoorGrid &grid) {
    auto place = [&](int id) { return grid.placeRandom(catalog, id); };
    auto near = [&](int id) { return grid.placeNearMarked(catalog, id); };
    auto stones = [&] { place(29); place(30); };
    auto cottage = [&](int id, bool extra = true) {
        if (grid.random().next() & 3) {
            near(id);
            if (extra && (grid.random().next() & 1)) near(49);
        } else { near(id); near(id); }
    };
    auto camp = [&](int id) { if (!(grid.random().next() & 3)) near(id); near(id); };
    switch (level) {
    case 2: near(46); if (!(grid.random().next() & 3)) near(47); near(47); stones(); break;
    case 3: cottage(48); place(44); stones(); break;
    case 4: near(160); near(45); place(162); cottage(47); camp(42); place(31); break;
    case 5: place(161); place(41); place(40); cottage(48); camp(43); stones(); break;
    case 6: place(163); place(38); place(39); cottage(47); camp(42); stones(); break;
    case 7: cottage(48); cottage(43, false); place(31); break;
    case 17: grid.place(catalog, 108, 1, 1); break;
    case 39: place(50); place(46); place(31); place(38); place(39); stones(); break;
    default: break;
    }
}
} // namespace
uint32_t initializeRetailWilderness(const WorldCatalog &catalog, const NativeActLayout &layout, int level,
                                   RetailOutdoorGrid &grid, const RetailPatternReader &reader) {
    if (catalog.level(level).act != 0 || catalog.level(level).levelType != 2)
        throw std::invalid_argument("Not a native Act I wilderness level");
    auto vertices = buildRetailBoundary(layout, level);
    uint32_t flags = layout.levels.at(level).outdoorFlags | markRetailAct1Cliffs(level, vertices);
    markRetailBoundaryLinks(layout, level, vertices, grid);
    placeRetailBorderPresets(catalog, layout, level, vertices, grid);
    if ((level >= 2 && level <= 7) || level == 39) secondary(catalog, level, flags, 0, grid, reader);
    if (level >= 2 && level <= 7) {
        if ((flags & 12) && riverFits(grid, grid.width() - 2)) river(catalog, grid, flags, grid.width() - 2);
        if ((flags & 0x20) && !(flags & 0x40)) {
            const bool transpose = (grid.random().next() & 1) != 0;
            bool added = false;
            for (int row = 0; row < grid.height() && !added; ++row)
                for (int column = 0; column < grid.width() && !added; ++column) {
                    const int x = transpose ? row : column, y = transpose ? column : row;
                    if (!grid.contains(x, y)) continue;
                    const int id = grid.cell(x, y).preset;
                    if (id == 16 || id == 17) {
                        grid.place(catalog, id == 16 ? 25 : 24, x, y);
                        flags |= 0x40; added = true;
                    }
                }
        }
        if ((flags & 28) && !(flags & 0x40)) {
            int y = grid.height() - 4;
            int x = grid.width() - int(((~uint8_t(flags) & 0x10) | 0x40) >> 4);
            const int choice = int(grid.random().next() & 3);
            if (choice & 1) x = 3;
            if (choice / 2) y = 3;
            grid.place(catalog, level == 2 ? 52 : 51, x, y);
            flags |= 0x40;
        }
        secondary(catalog, level, flags, 1, grid, reader);
        secondary(catalog, level, flags, 2, grid, reader);
        if ((flags & 0x10) && riverFits(grid, grid.width() / 2 - 1))
            river(catalog, grid, flags, grid.width() / 2 - 1);
        if (flags & 0x80) grid.place(catalog, 3, 0, 0, 1);
        if (flags & 0x100) grid.place(catalog, 3, grid.width() - 7, 0, 2);
        if (flags & 0x200) grid.place(catalog, 2, 0, 1, 1);
        if (flags & 0x400) grid.place(catalog, 2, 0, grid.height() - 6, 1);
        if (!(flags & 0x40)) {
            const auto &box = layout.levels.at(level);
            if (level == 2) {
                const auto &town = layout.levels.at(1);
                grid.placeFarAway(catalog, 52, -1, box.x, box.y, town.x, town.y, town.width, town.height, 1);
            } else grid.placeRandom(catalog, 51, -1, 1);
            flags |= 0x40;
        }
        secondary(catalog, level, flags, 3, grid, reader);
    } else if (level == 39) {
        secondary(catalog, level, flags, 1, grid, reader);
        secondary(catalog, level, flags, 2, grid, reader);
        secondary(catalog, level, flags, 3, grid, reader);
    }
    return flags;
}
void finishRetailWildernessPresets(const WorldCatalog &catalog, const NativeActLayout &layout, int level,
                                  RetailOutdoorGrid &grid) {
    if (level >= 3 && level <= 6) waypoint(catalog, layout, level, grid);
    if (level >= 2 && level <= 7) shrines(catalog, grid);
    special(catalog, level, grid);
}
} // namespace d2x
