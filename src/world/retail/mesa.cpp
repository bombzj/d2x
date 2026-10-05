#include "mesa.hpp"
#include "border_presets.hpp"
#include <array>
#include <stdexcept>

// D2MOO DrlgOutdoors::InitAct4OutdoorLevel. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
uint32_t initializeRetailMesa(const WorldCatalog &catalog, const NativeActLayout &layout,
    int level, RetailOutdoorGrid &grid, const RetailPatternReader &reader) {
    const auto vertices = buildRetailBoundary(layout, level);
    const auto flags = layout.levels.at(level).outdoorFlags;
    markRetailBoundaryLinks(layout, level, vertices, grid);
    if (level == 108) {
        constexpr std::array<int, 25> presets{
            836,836,836,836,836, 836,836,861,836,836,
            836,858,862,859,836, 836,836,860,836,836,
            836,836,857,836,836};
        for (size_t i = 0; i < presets.size(); ++i)
            grid.place(catalog, presets[i], 3 * int(i % 5), 3 * int(i / 5));
        return flags;
    }
    if (level < 104 || level > 106) throw std::invalid_argument("Not a native mesa level");
    placeRetailBorderPresets(catalog, layout, level, vertices, grid);
    if (flags & 0x400000) grid.place(catalog, 798, 0, 1);
    if (flags & 0x800000) grid.place(catalog, 798, 0, 4);
    for (int type : {1,2,3})
        applyRetailOutdoorSecondaryBorder(catalog, level, flags, type, 799, grid, reader);
    if (level == 106) grid.placeRandom(catalog, 811);
    constexpr int mesas[]{812,817,823}, pits[]{828,832,832};
    const int mesa = mesas[level - 104], pit = pits[level - 104];
    for (int offset : {0,1,1,2,2,3,3}) grid.placeRandom(catalog, mesa + offset);
    if (level == 105) grid.placeRandom(catalog, 822);
    for (int i = 0; i < 4; ++i) grid.placeRandom(catalog, mesa + 4);
    for (int offset : {0,1,1,2,2,3,3,3,3}) grid.placeRandom(catalog, pit + offset);
    return flags;
}
}
