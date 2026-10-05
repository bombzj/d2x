#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "world/map.hpp"
#include "world/outdoor/native_act_layout.hpp"
#include "world/native_map.hpp"
#include "resources/data_table.hpp"
#include "content/world/automap_data.hpp"
#include <span>

namespace d2x {
// MPQ-backed scene binding, separate from the server replica and local authority.
// All five acts reconstruct the same native room core used by offline maps.
class RemoteTown {
    Archives &archives_;
    TileLibraryCache libraries_;
    DataTable objects_;
    std::map<int, size_t> objectRows_;
    AutomapCatalog automap_;
    using Discovery = std::tuple<uint32_t, int, int, int, int>;
    std::set<Discovery> explored_;
    std::map<Discovery, std::set<int>> discoveredCels_;
    std::map<std::tuple<uint32_t, int, int>, OnlineAutomapTown> discoveredTowns_;
    std::unique_ptr<WorldCatalog> catalog_;
    const Map *map_{};
    std::optional<NativeActLayout> layout_;
    std::unique_ptr<NativeMapGenerator> nativeMap_;
    std::unique_ptr<Map> nativeTerrain_;
    std::optional<size_t> nativePlayerRoom_;
    std::optional<OnlinePoint> nativePlayerPosition_;
    uint64_t nativeSequence_{};
    std::string nativeReason_;
    std::map<int, std::string> nativeErrors_;
    OnlineSceneView view_;
    uint64_t gameGeneration_{~uint64_t{}}, areaGeneration_{~uint64_t{}}, revision_{~uint64_t{}};
    bool updateNative(const OnlineView &);
    void updateMapTargets(const OnlineView &);
    void updateAutomapView(const OnlineView &);

  public:
    explicit RemoteTown(Archives &a);
    void update(const OnlineView &);
    const OnlineSceneView &read() const { return view_; }
    const Map *map() const { return map_; }
    bool permits(const OnlineView &, OnlinePoint target) const;
    bool permitsInteraction(const OnlineView &, OnlineUnitKey target) const;
    void revealVisibleTiles(const OnlineView &, std::span<const size_t> instanceIndices);
};
} // namespace d2x
