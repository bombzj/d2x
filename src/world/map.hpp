#pragma once
#include "content/world/world_catalog.hpp"
#include "navigation.hpp"
#include "world/map_terrain.hpp"
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
    MapTerrain terrain;
    Grid grid;
    Vec spawn;
    std::vector<RoomBounds> rooms;
    RoomLayout activation;
    std::vector<Vec> warpArrivals;
    struct TombWall {
      int x = 0, y = 0;
      std::vector<MapCell> walls;
      std::array<uint8_t, 25> collision{};
      std::array<uint16_t,25> fullCollision{};
      std::vector<size_t> instances;
    };
    std::vector<TombWall> tombWalls;
    bool openTombWall(Vec position);
    void restoreTombWall();
    void load(Archives &archives, TileLibraryCache &cache, const MapRecipe &recipe, uint32_t seed);
    Vec actSpawn() const;
    int tileIndex(const MapCell &c, int x, int y) const;
};
} // namespace d2x
