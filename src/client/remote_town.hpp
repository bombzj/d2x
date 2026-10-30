#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "world/map.hpp"
#include "world/outdoor/native_act_layout.hpp"
#include "world/native_map.hpp"
#include "resources/data_table.hpp"
#include "content/world/automap_data.hpp"
#include "content/string_table.hpp"
#include "client/automap_exploration.hpp"
#include "client/automap_reveal.hpp"
#include <span>

namespace d2x {
// MPQ-backed scene binding, separate from the server replica and local authority.
// All five acts reconstruct the same native room core used by offline maps.
class RemoteTown {
  public:
    struct NavigationRoom {
        int level{},x{},y{},width{},height{};bool loaded{};size_t exploredTiles{};
        // Indices into this navigation snapshot, from the reconstructed RoomEx
        // near list. Adjacency guides exploration; collision still gates input.
        std::vector<size_t> near;
    };
  private:
    Archives &archives_;
    TileLibraryCache libraries_;
    DataTable objects_, monsters_, monsterSizes_, skills_, shrines_, petTypes_;
    std::map<int, size_t> shrineRows_;
    std::set<uint8_t> automapPetTypes_;
    std::map<int, size_t> objectRows_, monsterRows_;
    std::map<std::string, size_t, std::less<>> monsterSizeRows_;
    AutomapCatalog automap_;
    ClassicStrings strings_;
    using Discovery = std::tuple<uint32_t, int, int, int, int>;
    AutomapExploration explored_;
    std::map<int,uint32_t> actSeeds_;
    std::map<Discovery, std::set<int>> discoveredCels_;
    using NativeCells = AutomapCells;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, NativeCells> nativeAutomapCells_;
    size_t nativeAutomapCelCount_{};
    AutomapReveal revealed_;
    void rememberTile(const OnlineView &, int level, int x, int y, int type, bool wallArray, const MapCell &);
    std::map<std::tuple<uint32_t, int, int>, OnlineAutomapTown> discoveredTowns_;
    std::set<std::tuple<uint32_t, int, int>> preparedTownAutomaps_;
    std::shared_ptr<const WorldCatalog> catalog_;
    const Map *map_{};
    std::optional<NativeActLayout> layout_;
    std::unique_ptr<NativeMapGenerator> nativeMap_;
    bool nativeTerrainDirty_{true};
    std::unique_ptr<Map> nativeTerrain_;
    std::optional<size_t> nativePlayerRoom_;
    std::optional<OnlinePoint> nativePlayerPosition_;
    std::optional<OnlinePoint> displayPlayerPosition_;
    uint64_t nativeSequence_{};
    bool nativeLoadComplete_{};
    uint64_t nextNativeUpdateMilliseconds_{};
    std::string nativeReason_;
    std::map<int, std::string> nativeErrors_;
    struct UnitRoomEntry { size_t room{}; uint64_t assignment{}, entered{}; };
    std::map<OnlineUnitKey,UnitRoomEntry> unitRooms_;
    // Last positions learned from real client unit assignments, retained when
    // a unit/room leaves the current interest set; never a live GUID guarantee.
    std::map<std::tuple<uint16_t,uint8_t,uint16_t>,OnlinePoint> knownTargetPositions_;
    std::map<std::pair<uint16_t,uint16_t>,OnlinePoint> knownConnections_;
    std::map<uint16_t,OnlinePoint> knownWaypoints_;
    mutable std::vector<NavigationRoom> navigationRooms_;
    mutable std::map<int,std::vector<int>> navigationConnections_;
    mutable std::map<uint16_t,size_t> navigationSeen_;
    mutable std::tuple<uint64_t,uint64_t,uint64_t> navigationKey_{};
    mutable bool navigationCached_{};
    mutable uint64_t navigationCacheBuilds_{};
    void refreshNavigationCache() const;
    void updateUnitRooms(const OnlineView &);
    OnlineSceneView view_;
    uint64_t gameGeneration_{~uint64_t{}}, areaGeneration_{~uint64_t{}}, revision_{~uint64_t{}};
    std::optional<uint64_t> nextObjectTransition_;
    bool reportAutomapRoomsPd2_{};
    bool automapPlayerPresentPd2_{};
    OnlinePoint automapPositionPd2_{};
    std::vector<uint32_t> automapRoomCallbacksPd2_;
    void refresh_automap_rooms_pd2(const OnlineView &);
    void record_automap_room_pd2();
    bool updateNative(const OnlineView &);
    void updateMapTargets(const OnlineView &);
    void prepareTownAutomap(const OnlineView &);
    MapSceneView automapScene(const OnlineView &) const;
    void updateAutomapView(const OnlineView &);

  public:
    explicit RemoteTown(Archives &a, bool reportAutomapRoomsPd2 = false);
    void update(const OnlineView &, std::optional<Vec> displayPosition = {});
    const OnlineSceneView &read() const { return view_; }
    Grid navigationGrid(const OnlineView &,std::optional<OnlineUnitKey> ignored = {}) const;
    const Map *map() const { return map_; }
    // Already reconstructed logical walking links, including an adjoining room
    // before it becomes active. Global subtile coordinates; no room generation.
    struct WalkingBoundary {uint16_t destination{};int side{},plane{},start{},end{};};
    std::vector<WalkingBoundary> walkingBoundaries() const;
    const std::vector<NavigationRoom> &navigationRooms() const;
    std::optional<OnlinePoint> knownTargetPosition(uint16_t area,uint8_t type,uint16_t identity) const;
    std::optional<OnlinePoint> knownConnectionPosition(uint16_t from,uint16_t to) const;
    std::optional<OnlinePoint> knownWaypointPosition(uint16_t area) const;
    uint64_t navigationRevision() const;
    uint64_t navigationCacheBuilds() const {return navigationCacheBuilds_;}
    // The layout's resolved Vis slots override the table's static slots.
    const std::map<int,std::vector<int>> &navigationConnections() const;
    bool permits(const OnlineView &, OnlinePoint target) const;
    // Native player-room / active-neighbor order, then room-unit prepend order.
    std::vector<OnlineUnitKey> nearbyUnits(const OnlineView &, OnlinePoint center, int radius) const;
    bool permitsInteraction(const OnlineView &, OnlineUnitKey target) const;
    bool interactionReady(const OnlineView &, OnlineUnitKey target, std::optional<Vec> displayOrigin = {}, bool approaching = false) const;
    void revealVisibleTiles(const OnlineView &, std::span<const size_t> instanceIndices);
    // Observer-only overlay; does not enter discovery, navigation or live DRLG.
    void updateAutomapReveal(const OnlineView &, bool enabled, AutomapPresetCellSelector);
    std::vector<uint32_t> take_automap_room_reveals_pd2();
    AutomapDiscovery discovery() const;
    void restoreDiscovery(const AutomapDiscovery &, const OnlineView &);
};
} // namespace d2x
