#pragma once
#include <array>
#include <string>

namespace d2x {
// Resolved MPQ component codes, independent of packets, equipment authority and GPU handles.
struct ActorAppearance {
    std::string token, weapon;
    std::array<std::string, 16> components;
};
} // namespace d2x
