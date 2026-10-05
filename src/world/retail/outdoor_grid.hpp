#pragma once
#include "content/world/world_catalog.hpp"
#include "world/generation_seed.hpp"
#include <map>
#include <utility>
#include <vector>

namespace d2x {
struct RetailOutdoorCell {
    int preset{};
    uint32_t links{}, flags{}, auxiliary{};
};
struct RetailRoomSeed {
    Seed random;
    uint32_t initial{};
};
// DRLGROOM_AllocRoomEx consumes the level stream once, then the room stream
// once. The retained allocation stream is used for PickSubThemes / lazy units;
// InitRoomGrids separately resets low=initial, high=666 before tile generation.
RetailRoomSeed allocateRetailRoomSeed(Seed &levelRandom);

// Four native outdoor grids represented by cells. Coordinates are eight-tile
// cells, not network subtiles. This is a DRLG phase, not a playable map.
class RetailOutdoorGrid {
    int width_{}, height_{};
    std::vector<RetailOutdoorCell> cells_;
    std::map<int, int> pickedFiles_;
    Seed random_;

  public:
    RetailOutdoorGrid(int tileWidth, int tileHeight, uint32_t startSeed, int level);
    int width() const { return width_; }
    int height() const { return height_; }
    bool contains(int x, int y) const;
    RetailOutdoorCell &cell(int x, int y);
    const RetailOutdoorCell &cell(int x, int y) const;
    Seed &random() { return random_; }
    std::vector<std::pair<int, int>> shuffledInterior();
    void clear(int x, int y);
    void blank(int x, int y);
    bool canPlace(const WorldCatalog &, int preset, int x, int y,
                  int clearance = 0, uint8_t edges = 15) const;
    // markBorder is the caller's evaluated native border-preset condition;
    // overlapping border writes are legal and must not use canPlace().
    void place(const WorldCatalog &, int preset, int x, int y,
               int pickedFile = -1, bool markBorder = false);
    bool placeRandom(const WorldCatalog &, int preset, int pickedFile = -1,
                     int clearance = 0, uint8_t edges = 15);
    bool placeNearMarked(const WorldCatalog &, int preset, int pickedFile = -1);
    bool placeFarAway(const WorldCatalog &, int preset, int pickedFile,
                      int worldX, int worldY, int referenceX, int referenceY,
                      int referenceWidth, int referenceHeight,
                      int clearance = 0, uint8_t edges = 15);
};
} // namespace d2x
