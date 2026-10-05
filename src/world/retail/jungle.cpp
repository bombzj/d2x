#include "jungle.hpp"
#include "boundary.hpp"
#include <array>
#include <stdexcept>
// D2MOO DrlgOutJung / DrlgOutPlace::BuildKurast. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
void randomFill(const WorldCatalog &catalog, RetailOutdoorGrid &grid, int first, int last, int maximum) {
    int count = 0;
    for (const auto &[x,y] : grid.shuffledCells()) {
        const int id = first + grid.random().below(last - first + 1);
        if (grid.canPlace(catalog,id,x,y)) {
            grid.place(catalog,id,x,y);
            if (maximum > 0 && ++count >= maximum) break;
        }
    }
}
void cityBorders(const WorldCatalog &catalog, const NativeActLayout &layout, int level, RetailOutdoorGrid &grid) {
    const int w = grid.width() - 1, h = grid.height() - 1;
    const int base = level == 79 ? 605 : level == 80 ? 619 : 636;
    const int north = level == 79 ? (layout.jungleInterlink ? 1 : w-1) :
                      level == 80 ? (layout.jungleInterlink ? w-1 : 1) : w/2;
    const int south = level == 79 ? w/2 : level == 80 ? (layout.jungleInterlink ? 1 : w-1) :
                      (layout.jungleInterlink ? w-1 : 1);
    auto place = [&](int id,int x,int y) { grid.place(catalog,id,x,y); };
    if (level == 80) for (int x = 1; x < w; ++x) {
        place(base + 8*(x==north),x,0); place(base+1+8*(x==south),x,h);
    } else {
        for (int x = 1; x < w; x += 1 + (level == 81 && x == north)) place(base + 8*(x==north),x,0);
        for (int x = 1; x < w; x += 1 + (level == 79 && x == south)) place(base+1+8*(x==south),x,h);
    }
    for (int y = 1; y < h; ++y) { place(base+2,w,y); place(base+3,0,y); }
    place(base+5,0,0); place(base+4,w,0); place(base+7,0,h); place(base+6,w,h);
}
}
uint32_t initializeRetailJungle(const WorldCatalog &catalog, const NativeActLayout &layout,
    int level, RetailOutdoorGrid &grid) {
    const auto vertices = buildRetailBoundary(layout,level);
    markRetailBoundaryLinks(layout,level,vertices,grid);
    if (level >= 76 && level <= 78) {
        const auto &data = layout.jungles.at(level);
        const auto &forest = catalog.level(76);
        const int width = forest.width/32, height = forest.height/32;
        const int selected = grid.random().below(data.clearings == 3 ? 6 : 2);
        constexpr std::array<int,18> variants{0,1,2,1,0,2,0,2,1,1,2,0,2,0,1,2,1,0};
        int clearing = 0;
        if (data.presets.size() != size_t(width*height)) throw std::runtime_error("Invalid native jungle definitions");
        for (int y = 0; y < height; ++y) {
            if ((level == 76 && y == height-1) || (level == 78 && y == 0)) {
                grid.place(catalog,level == 76 ? 573 : 574,0,4*y,data.presets[size_t(width*y+1)] == 0);
                continue;
            }
            for (int x = 0; x < width; ++x) {
                int id = data.presets[size_t(width*y+x)], file = -1;
                if (id > 574) {
                    if (clearing >= 3) throw std::runtime_error("Invalid native jungle clearing count");
                    id += 10*(level-76); file = variants[size_t(clearing++ + 3*selected)];
                }
                if (id) grid.place(catalog,id,4*x,4*y,file);
            }
        }
    } else if (level >= 79 && level <= 81) {
        cityBorders(catalog,layout,level,grid);
        if (level != 79) {
            const int sewer = level == 80 ? 629 : 646, y = level == 80 ? 3 : grid.height()-4;
            grid.place(catalog,sewer,3,y,0); grid.place(catalog,sewer,grid.width()-4,y,1);
            const int temple = level == 80 ? 630 : 647;
            grid.placeRandom(catalog,temple,0); grid.placeRandom(catalog,temple,1);
        }
        grid.placeRandom(catalog,631,0);
        const int small = level == 79 ? 615 : level == 80 ? 632 : 648;
        randomFill(catalog,grid,small+3,small+3,4);
        randomFill(catalog,grid,small+1,small+2,0);
        randomFill(catalog,grid,small,small,0);
    } else if (level == 82) grid.place(catalog,652,0,0,0);
    else if (level == 83) {
        grid.place(catalog,653,0,0); grid.place(catalog,654,2,0); grid.place(catalog,655,6,0);
        grid.place(catalog,656,0,4); grid.place(catalog,657,2,4); grid.place(catalog,658,6,4);
    } else throw std::runtime_error("Unsupported native jungle/city level");
    return layout.levels.at(level).outdoorFlags;
}
} // namespace d2x
