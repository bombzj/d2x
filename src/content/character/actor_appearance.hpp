#pragma once
#include <array>
#include <string>

namespace d2x {
// Resolved MPQ component codes, independent of packets, equipment authority and GPU handles.
struct ActorAppearance {
    std::string token, weapon;
    std::array<std::string, 16> components;
    std::array<std::array<int, 2>, 16> componentColors{};
    // The original body layers remain drawable with incomplete equipment effects.
    bool equipmentEffectsKnown = true;
};
} // namespace d2x
