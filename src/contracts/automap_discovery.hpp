#pragma once
#include "world/identity.hpp"
#include <cstdint>
#include <map>
#include <vector>

namespace d2x {
struct AutomapLayer {
    int width = 0, height = 0;
    uint64_t layoutFingerprint = 0;
    std::vector<uint8_t> seen;
};
using AutomapLayers = std::map<RegionId, AutomapLayer>;
struct AutomapRememberedCell {
    uint32_t seed{};
    int act{}, region{}, x{}, y{};
    std::vector<int> cels;
};
struct AutomapRememberedTown {
    uint32_t seed{};
    int act{}, region{}, variant{}, x{}, y{};
};
// Observer discovery only; never grants server room interest or collision.
struct AutomapDiscovery {
    int difficulty{};
    uint64_t contentFingerprint{};
    std::map<int, uint32_t> actSeeds;
    AutomapLayers layers;
    std::vector<AutomapRememberedCell> cells;
    std::vector<AutomapRememberedTown> towns;
};
} // namespace d2x
