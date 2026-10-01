#pragma once
#include "content/world/world_catalog.hpp"
#include "navigation.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include <memory>
#include <tuple>
namespace d2x {
class TileLibraryCache {
    Archives &archives_;
    std::map<std::string, std::shared_ptr<const std::vector<Tile>>> libraries_;

  public:
    explicit TileLibraryCache(Archives &archives) : archives_(archives) {}
    std::shared_ptr<const std::vector<Tile>> load(const std::string &path);
};
struct Map {
    using RoomBounds = d2x::RoomBounds;
    MapData data;
    std::vector<std::shared_ptr<const std::vector<Tile>>> libraries;
    std::vector<const Tile *> tiles;
    std::map<uint32_t, std::vector<int>> lookup;
    std::vector<std::map<uint32_t, std::vector<int>>> scopedLookup;
    // Chosen once at load: collision, automap and rendering read the same DT1 variant.
    std::map<std::tuple<int, int, size_t, uint32_t>, int> tileChoices;
    Grid grid;
    std::string name, path;
    Vec spawn;
    std::vector<RoomBounds> rooms;
    RoomLayout activation;
    std::vector<Vec> warpArrivals;
    int unresolved = 0;
    void load(Archives &archives, TileLibraryCache &cache, const MapRecipe &recipe, uint32_t seed);
    Vec actSpawn() const;
    int tileIndex(const MapCell &c, int x, int y) const;
};
} // namespace d2x
