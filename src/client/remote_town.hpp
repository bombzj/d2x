#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "world/map.hpp"

namespace d2x {
// MPQ-backed scene binding, separate from the server replica and local authority.
// A unique authored preset and translation must match real stationary landmarks
// and original eight-tile preset-room anchors before terrain can be displayed.
class RemoteTown {
    struct Candidate {
        MapRecipe recipe;
        MapData data;
    };
    Archives &archives_;
    TileLibraryCache libraries_;
    std::vector<Candidate> candidates_;
    std::unique_ptr<Map> map_;
    OnlineSceneView view_;
    uint64_t gameGeneration_{~uint64_t{}}, areaGeneration_{~uint64_t{}}, revision_{~uint64_t{}};
    bool collisionInvariant(const MapTerrain &) const;

  public:
    explicit RemoteTown(Archives &a) : archives_(a), libraries_(a) {}
    void update(const OnlineView &);
    const OnlineSceneView &read() const { return view_; }
    const Map *map() const { return map_.get(); }
    bool permits(const OnlineView &, OnlinePoint target) const;
};
} // namespace d2x
