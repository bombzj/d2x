#pragma once
#include <cstdint>
#include <vector>
namespace d2x {
class Archives;
struct Tile;
uint64_t tileLibraryId(const Archives &, const std::vector<Tile> &);
struct TileLibraryUsage {
    uint64_t libraries{}, pixelBytes{};
};
TileLibraryUsage tileLibraryUsage(const Archives &archives);
} // namespace d2x
