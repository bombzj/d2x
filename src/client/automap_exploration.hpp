#pragma once
#include "contracts/map.hpp"
#include "contracts/automap_discovery.hpp"
#include <cstddef>
#include <map>
#include <span>

namespace d2x {
struct AutomapVisibleCell { int slot = 0, x = 0, y = 0; };
// Observer-owned discovery, independent of authority state and GPU resources.
class AutomapExploration {
    AutomapLayers layers_;
    std::map<RegionId,size_t> revealedCounts_;
    uint64_t revision_{};
  public:
    const AutomapLayers &layers() const { return layers_; }
    uint64_t revision() const { return revision_; }
    void clear() { if(!layers_.empty()) {layers_.clear();revealedCounts_.clear();++revision_;} }
    size_t revealedCount(RegionId id) const;
    void restore(AutomapLayers layers);
    // True when remembered geometry no longer matches a loaded region.
    bool reveal(const MapSceneView &scene, std::span<const AutomapVisibleCell> visible,bool revealSafe=true);
};
} // namespace d2x
