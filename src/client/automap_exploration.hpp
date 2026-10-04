#pragma once
#include "contracts/map.hpp"
#include <map>
#include <span>

namespace d2x {
struct AutomapLayer {
    int width = 0, height = 0;
    uint64_t layoutFingerprint = 0;
    std::vector<uint8_t> seen;
};
using AutomapLayers = std::map<RegionId, AutomapLayer>;
struct AutomapVisibleCell { int slot = 0, x = 0, y = 0; };
// Observer-owned discovery, independent of authority state and GPU resources.
class AutomapExploration {
    AutomapLayers layers_;
  public:
    const AutomapLayers &layers() const { return layers_; }
    void clear() { layers_.clear(); }
    void restore(AutomapLayers layers);
    // True when remembered geometry no longer matches a loaded region.
    bool reveal(const MapSceneView &scene, std::span<const AutomapVisibleCell> visible);
};
} // namespace d2x
