#pragma once
#include <array>
#include <string>

namespace d2x {
// Resolved MPQ component codes, independent of packets, equipment authority and GPU handles.
struct ActorAppearance {
    std::string token, weapon;
    std::array<std::string, 16> components;
    // Geometry can be fully known while per-item coloring/transparency is not.
    // Missing decorative effects must not invalidate the resolved body layers.
    bool equipmentEffectsKnown = true;
};
} // namespace d2x
