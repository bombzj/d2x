#pragma once
#include "world/identity.hpp"
#include <cstdint>
#include <array>
#include <compare>
#include <tuple>
#include <map>
#include <vector>
#include <optional>
#include <string>

namespace d2x {
// D2Client's six-byte saved cell, in projected client pixels divided by ten.
struct NativeAutomapCell {
    uint16_t cel{};
    int16_t x{}, y{};
    auto operator<=>(const NativeAutomapCell &other) const {
        return std::tie(y,x,cel) <=> std::tie(other.y,other.x,other.cel);
    }
    bool operator==(const NativeAutomapCell &) const = default;
};
struct NativeAutomapLayer {
    uint32_t seed{}, layer{}, flags{}, objectSeed{};
    std::array<std::vector<NativeAutomapCell>, 4> cells; // floors, walls, objects, extras
};
struct AutomapLayer {
    int width = 0, height = 0;
    uint64_t layoutFingerprint = 0;
    std::vector<uint8_t> seen;
};
using AutomapLayers = std::map<RegionId, AutomapLayer>;
struct NavigationDiscovery {
    std::string profile;
    uint32_t seed{},objectSeed{};
    std::map<int,uint32_t> actSeeds;
    AutomapLayers observed;
    // Last actual assignments, without live GUIDs: area/type/class/x/y,
    // from/to/x/y, and area/x/y. These are hints, never interaction authority.
    std::vector<std::array<uint16_t,5>> targets;
    std::vector<std::array<uint16_t,4>> connections;
    std::vector<std::array<uint16_t,3>> waypoints;
};
// Observer discovery only; never grants server room interest or collision.
struct AutomapDiscovery {
    std::vector<NativeAutomapLayer> nativeLayers;
    std::optional<NavigationDiscovery> navigation;
};
} // namespace d2x
