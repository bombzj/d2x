#pragma once
#include "content/world/world_catalog.hpp"
#include <map>
#include <set>
#include <vector>

namespace d2x {
// Native tile coordinates, independent of transport, presentation and local AI.
// Placement is only the first DRLG phase: bounds do not prove terrain equivalence.
struct NativeLevelPlacement {
    int level{}, x{}, y{}, width{}, height{}, direction{-1}, alignment{-1};
    std::optional<int> presetVariant;
    uint32_t outdoorFlags{};
    bool containsSubtile(int px, int py) const {
        return px >= x * 5 && py >= y * 5 && px < (x + width) * 5 && py < (y + height) * 5;
    }
};
struct NativeActLayout {
    uint32_t startSeed{};
    std::optional<int> staffTomb, bossTomb;
    bool jungleInterlink{};
    struct Jungle { std::vector<int> presets; int clearings{}; };
    std::map<int, Jungle> jungles;
    std::map<int, NativeLevelPlacement> levels;
    // Placement graph edges; not a reconstructed native Vis/Warp array.
    std::set<std::pair<int, int>> links;
    struct ConnectionSlots { std::array<int, 8> visible{}, warps{}; };
    struct Neighbor {
        int level{}, direction{}; // Native orth direction: west, north, east, south.
        bool preset{};
    };
    std::map<int, ConnectionSlots> connections;
    std::map<int, std::vector<Neighbor>> neighbors;
};
NativeActLayout placeNativeAct(const WorldCatalog &, int act, uint32_t initialSeed);
} // namespace d2x
