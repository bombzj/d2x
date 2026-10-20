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
#include <span>

namespace d2x {
// MPQ-backed scene binding, separate from the server replica and local authority.
// All five acts reconstruct the same native room core used by offline maps.
class RemoteTown {
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
    using NativeCells = std::array<std::set<NativeAutomapCell>, 4>;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, NativeCells> nativeAutomapCells_;
    size_t nativeAutomapCelCount_{};
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
    std::string nativeReason_;
    std::map<int, std::string> nativeErrors_;
    OnlineSceneView view_;
    uint64_t gameGeneration_{~uint64_t{}}, areaGeneration_{~uint64_t{}}, revision_{~uint64_t{}};
    std::optional<uint64_t> nextObjectTransition_;
    bool updateNative(const OnlineView &);
    void updateMapTargets(const OnlineView &);
    void prepareTownAutomap(const OnlineView &);
    MapSceneView automapScene(const OnlineView &) const;
    void updateAutomapView(const OnlineView &);

  public:
    explicit RemoteTown(Archives &a);
    void update(const OnlineView &, std::optional<Vec> displayPosition = {});
    const OnlineSceneView &read() const { return view_; }
    const Map *map() const { return map_; }
    bool permits(const OnlineView &, OnlinePoint target) const;
    bool permitsInteraction(const OnlineView &, OnlineUnitKey target) const;
    bool interactionReady(const OnlineView &, OnlineUnitKey target, std::optional<Vec> displayOrigin = {}, bool approaching = false) const;
    void revealVisibleTiles(const OnlineView &, std::span<const size_t> instanceIndices);
    AutomapDiscovery discovery() const;
    void restoreDiscovery(const AutomapDiscovery &, const OnlineView &);
};
} // namespace d2x
