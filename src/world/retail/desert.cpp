#include "desert.hpp"
#include "border_presets.hpp"
#include <array>
#include <span>
#include <stdexcept>

// D2MOO DrlgOutDesr: placement order and random consumption.
// Preset dimensions, file counts, substitutions and resources come from MPQ.
// MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
void variants(const WorldCatalog &catalog, RetailOutdoorGrid &grid,
              std::initializer_list<int> ids, bool files = false) {
    const std::span<const int> list(ids.begin(), ids.size());
    const int start = grid.random().below(int(list.size()));
    for (size_t i = 0; i < list.size(); ++i) {
        const int id = list[(size_t(start) + i) % list.size()];
        if (files) for (int file = 0; file < catalog.presets().at(id).files; ++file)
            grid.placeRandom(catalog, id, file);
        else grid.placeRandom(catalog, id);
    }
}
struct Placement { int id, file, x, y; };
void cliffs(const WorldCatalog &catalog, RetailOutdoorGrid &grid) {
    static constexpr Placement rows[8][5]{
        {{376,1,0,4},{378,-1,2,4},{377,-1,4,4},{377,-1,6,4},{376,2,8,4}},
        {{376,1,0,4},{377,-1,2,4},{378,-1,4,4},{377,-1,6,4},{376,2,8,4}},
        {{376,1,0,4},{377,-1,2,4},{377,-1,4,4},{378,-1,6,4},{376,2,8,4}},
        {{376,2,8,4},{377,-1,6,4},{382,-1,4,4},{381,-1,4,6},{379,2,4,8}},
        {{376,2,8,4},{378,-1,6,4},{382,-1,4,4},{380,-1,4,6},{379,2,4,8}},
        {{379,1,4,0},{381,-1,4,2},{380,-1,4,4},{380,-1,4,6},{379,2,4,8}},
        {{379,1,4,0},{380,-1,4,2},{381,-1,4,4},{380,-1,4,6},{379,2,4,8}},
        {{379,1,4,0},{380,-1,4,2},{380,-1,4,4},{381,-1,4,6},{379,2,4,8}}};
    for (const auto &p : rows[grid.random().next() & 7]) grid.place(catalog, p.id, p.x, p.y, p.file);
}
void canyon(const WorldCatalog &catalog, RetailOutdoorGrid &grid) {
    static constexpr Placement entries[]{
        {384,0,8,0},{383,2,6,0},{383,1,4,0},{383,0,2,0},{387,0,0,0},
        {385,0,0,2},{385,1,0,4},{385,2,0,6},{386,0,0,8}};
    for (const auto &p : entries) grid.place(catalog, p.id, p.x, p.y, p.file);
    grid.place(catalog, 394, 4, 4);
}
void canyonFills(const WorldCatalog &catalog, RetailOutdoorGrid &grid) {
    variants(catalog, grid, {401,402,406,407,403});
    variants(catalog, grid, {392,393}, true);
}
}
uint32_t initializeRetailDesert(const WorldCatalog &catalog, const NativeActLayout &layout,
    int level, RetailOutdoorGrid &grid, const RetailPatternReader &reader) {
    if (catalog.level(level).levelType != 16) throw std::invalid_argument("Not a native desert level");
    const auto vertices = buildRetailBoundary(layout, level);
    const auto flags = layout.levels.at(level).outdoorFlags;
    markRetailBoundaryLinks(layout, level, vertices, grid);
    placeRetailBorderPresets(catalog, layout, level, vertices, grid);
    auto borders = [&] {
        for (int type : {2,1,3}) applyRetailOutdoorSecondaryBorder(catalog, level, flags, type, 364, grid, reader);
    };
    auto exit = [&](int id) {
        if (!grid.placeRandom(catalog, id)) throw std::runtime_error("Native desert exit placement failed");
    };
    auto waypoint = [&] { reserveRetailOutdoorWaypoint(catalog, layout, level, grid); };
    auto shrines = [&] { reserveRetailOutdoorShrines(catalog, grid); };
    switch (level) {
    case 41:
        for (const auto &n : layout.neighbors.at(level)) if (n.level == 40) {
            if (n.direction == 3) grid.place(catalog, 363, 0, grid.height() - 1);
            else grid.place(catalog, 362, grid.width() - 1, 0);
            break;
        }
        borders(); exit(388); shrines(); variants(catalog, grid, {395,411,401,402,399,398,403}); break;
    case 42:
        cliffs(catalog, grid); borders(); exit(388); waypoint(); shrines();
        variants(catalog, grid, {395,411,400,398}); variants(catalog, grid, {404,405,406,407}, true); break;
    case 43:
        cliffs(catalog, grid); borders(); exit(390); variants(catalog, grid, {396,397}); waypoint(); shrines();
        variants(catalog, grid, {411,399,398,403}); variants(catalog, grid, {395}, true); variants(catalog, grid, {395}, true); break;
    case 44:
        cliffs(catalog, grid); borders(); exit(412); variants(catalog, grid, {413,408,409,410}); waypoint(); shrines();
        variants(catalog, grid, {395,400,398,404,405}); variants(catalog, grid, {411}, true); variants(catalog, grid, {411}, true); break;
    case 45: exit(389); break;
    case 46: canyon(catalog, grid); borders(); shrines(); canyonFills(catalog, grid); break;
    case 134: grid.place(catalog, 394, 4, 4); borders(); canyonFills(catalog, grid); shrines(); break;
    default: throw std::runtime_error("Unsupported native desert level");
    }
    return flags;
}
} // namespace d2x
