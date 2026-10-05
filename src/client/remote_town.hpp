#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "world/map.hpp"
#include "world/outdoor/native_act_layout.hpp"
#include "world/native_map.hpp"

namespace d2x {
// MPQ-backed scene binding, separate from the server replica and local authority.
// All five acts reconstruct the same native room core used by offline maps.
class RemoteTown {
    Archives &archives_;
    TileLibraryCache libraries_;
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

  public:
    explicit RemoteTown(Archives &a) : archives_(a), libraries_(a) {}
    void update(const OnlineView &);
    const OnlineSceneView &read() const { return view_; }
    const Map *map() const { return map_; }
    bool permits(const OnlineView &, OnlinePoint target) const;
};
} // namespace d2x
