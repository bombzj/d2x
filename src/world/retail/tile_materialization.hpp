#pragma once
#include "room_data.hpp"
#include "preset_room.hpp"
#include <deque>
#include <optional>
#include <span>
#include <set>
#include <tuple>

namespace d2x {
// Indices remain stable when another room updates an owner's seam tile. The
// native next link is shared by map-link and warp chains; do not deduplicate it.
struct RetailMaterializedTile {
    size_t owner{};
    int x{}, y{}, type{};
    uint32_t packed{}, flags{};
    const Tile *tile{};
    std::optional<size_t> next;
};
struct RetailTileRoom {
    int level{};
    RetailRoom room;
    std::shared_ptr<RetailTileSelector> libraries;
    std::array<int, 8> warpSlots{};
    std::vector<size_t> floors{}, walls{}, shadows{};
    std::array<std::optional<size_t>, 2> mapLinks{}; // non-floor, floor
    struct Warp {
        int id{};
        const WarpRecord *definition{};
        std::optional<size_t> visible, lit;
        std::optional<size_t> destination{};
    };
    std::vector<Warp> warps{};
    std::vector<size_t> near{};
    // Active-room adjacency is reordered on allocation and swap-erased on
    // release; it can differ from the immutable logical near-room order.
    std::vector<size_t> activeNear{};
    struct Unit { int type{}, id{}, x{}, y{}; }; // global subtile coordinates
    std::vector<Unit> units{};
    std::vector<RetailPresetUnit> authoredUnits{};
    std::vector<RoofPopup> roofPopups{};
    struct Animation { std::vector<size_t> frames; int speed{}; };
    std::vector<Animation> animations{};
    MapData grids{}; // Logical layers for offline arrivals/automap/exit consumers.
    bool loaded{};
};
struct RetailRoomCollision {
    int x{}, y{}, width{}, height{}; // subtiles
    std::vector<uint16_t> flags;
};
// This session consumes rooms in the caller's explicit activation order and
// accepts the native near-room list and established warp links. GS 07 receipt
// order alone is not evidence for either. Failure discards the whole session.
class RetailTileMaterializer {
    const WorldCatalog &catalog_;
    std::deque<RetailTileRoom> rooms_;
    std::deque<RetailMaterializedTile> tiles_;
    std::map<size_t, RetailRoomCollision> collisions_;
    std::map<RetailPresetKey, std::vector<RetailPresetUnit>> presetUnits_;
    bool failed_{};
    struct Change { size_t tile{}; const Tile *before{}, *after{}; };
    std::vector<Change> changes_;
    void initializeFlags(size_t current, size_t tile, uint32_t packed);
    void changeCollision(size_t owner, size_t tile, const Tile *after);
    void door(size_t current, std::optional<size_t> tile, int type, uint32_t packed, int x, int y);
    size_t create(size_t current, int type, uint32_t packed, int x, int y,
        std::optional<size_t> *head = nullptr, const Tile *selected = nullptr, bool wallArray = false);
    std::optional<size_t> find(size_t current, std::span<const size_t> near,
        int type, uint32_t packed, int x, int y) const;
    void update(size_t current, size_t tile, int type, uint32_t packed, int x, int y);
    void linked(size_t current, int type, uint32_t packed, int x, int y);
    const WarpRecord *warpDefinition(size_t current, uint32_t packed, int type) const;
    RetailTileRoom::Warp *warpLink(size_t current, uint32_t packed, int type);
    bool addWarpUnit(size_t current, uint32_t packed, int type, int x, int y);
    void wallWarp(size_t current, size_t tile, uint32_t packed, int type);
    void floorWarp(size_t current, uint32_t packed, int type, int x, int y);
    void layer(size_t current, std::span<const size_t> near, std::span<const MapCell>,
        int stride, bool orientations, bool fillBlanks, bool killX, bool killY);
    void animate(size_t current, int speed);
  public:
    explicit RetailTileMaterializer(const WorldCatalog &catalog) : catalog_(catalog) {}
    // Supply only warp IDs for which native room-to-room warp links exist.
    size_t registerRoom(int level, const RetailRoom &, std::shared_ptr<RetailTileSelector>,
        const std::array<int, 8> &warpSlots,
        std::span<const int> linkedWarpIds = {});
    // Register each level's complete room list in creation order. The callback
    // lazily allocates destination levels; it must not activate their tiles.
    // Reconstructs the native reverse level-list walk, positional bubble passes,
    // repeated Vis-slot pairing and smooth/warp room links without RNG draws.
    std::vector<size_t> establishNear(size_t room, const NativeActLayout &,
        const std::function<void(int)> &ensureLevel);
    void loadOutdoor(size_t room, const RetailOutdoorRoomData &, std::span<const size_t> near);
    // Called at UNTILE preparation, before any in-sight room creates tiles.
    void preparePresetUnits(size_t room, bool preloaded,
        const std::function<std::vector<RetailPresetUnit>(Seed &)> &loadLazyUnits = {},
        std::span<const RetailPresetUnit> preloadedUnits = {},
        const std::function<std::vector<RetailPresetUnit>()> &clientUnits = {});
    void loadPreset(size_t room, const RetailPresetRoomData &, std::span<const size_t> near,
        const std::function<std::vector<RetailPresetUnit>(Seed &)> &loadLazyUnits = {},
        std::span<const RetailPresetUnit> preloadedUnits = {});
    const auto &rooms() const { return rooms_; }
    const auto &tiles() const { return tiles_; }
    void releaseRoom(size_t room);
    bool valid() const { return !failed_; }
    void activateCollision(size_t room, std::span<const size_t> activeNear);
    const RetailRoomCollision *collision(size_t room) const;
    // Changes are applied immediately to registered active collision grids.
    const auto &changes() const { return changes_; }
};
// Native position bubble pass, deliberately not a lexicographic std::sort.
void sortRetailNearRooms(std::vector<size_t> &, const std::deque<RetailTileRoom> &);
// Initial grid only: includes all three tile arrays, even hidden/lit tiles.
// No synthetic outer barrier or actor collision.
RetailRoomCollision projectRetailRoomCollision(const RetailTileMaterializer &, size_t room,
    std::span<const size_t> activeNear);
} // namespace d2x
