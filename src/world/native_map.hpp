#pragma once
#include "native_room_activation.hpp"
#include "retail/tile_materialization.hpp"
#include "maze.hpp"

namespace d2x {
struct NativeMapSnapshot {
    Map map;
    int tileX{}, tileY{};
    std::vector<uint16_t> collision; // Complete native flags, including unseen cells.
    std::vector<int> levels;
};
// Shared MPQ generation session. Network/presentation provide coordinates and
// lifecycle events; no local actors, simulated combat or socket ownership.
class NativeMapGenerator {
    Archives &archives_;
    const WorldCatalog &catalog_;
    TileLibraryCache &cache_;
    int act_;
    uint32_t mapSeed_;
    int difficulty_;
    NativeActLayout layout_;
    RetailPresetScan identities_;
    RetailTileMaterializer tiles_;
    struct Level {
        std::optional<RetailOutdoorLayout> outdoor;
        std::optional<RetailPresetLevel> preset;
        std::optional<NativeMazeLevel> maze;
        std::vector<size_t> rooms;
    };
    std::map<int, Level> levels_;
    std::map<std::string, MapData> patterns_;
    std::set<int> allocating_;
    NativeRoomActivation activation_;
    bool failed_{};
    const MapData &pattern(const std::string &);
    void ensureLevel(int);
    std::vector<size_t> near(size_t);
    void prepare(size_t);
    void create(size_t);
    size_t roomAt(int level, int tileX, int tileY);
  public:
    NativeMapGenerator(Archives &, const WorldCatalog &, TileLibraryCache &, int act, uint32_t mapSeed,
        int difficulty = 0);
    size_t reveal(int level, int tileX, int tileY);
    // Allocation order; MPQ AutoMap presets also initialize their native rooms.
    std::vector<size_t> levelRooms(int level);
    void hide(int level, int tileX, int tileY);
    // Position lookups only inspect allocated rooms; they never infer a level
    // or generate distant levels from a coordinate supplied by the caller.
    std::optional<size_t> clientRoom(int subtileX, int subtileY) const;
    void changeClientRoom(std::optional<size_t> previous, std::optional<size_t> next);
    NativeMapSnapshot snapshot(int currentLevel, bool continuous = true) const;
    // Offline consumers use the same room lifecycle and DT1 selection.
    NativeMapSnapshot completeLevel(int level);
    MapRecipe recipe(int level);
    const auto &layout() const { return layout_; }
    const auto &tiles() const { return tiles_; }
    const auto &activation() const { return activation_; }
    bool valid() const { return !failed_ && tiles_.valid() && activation_.valid(); }
};
} // namespace d2x
