#pragma once
#include "core/bytes.hpp"
#include "d2s_header.hpp"
#include <array>
#include <span>

namespace d2x {
struct D2sFixedSections {
    std::array<uint8_t, 298> quests{};
    std::array<uint8_t, 80> waypoints{};
    std::array<uint8_t, 52> introductions{};
};
D2sFixedSections readD2sFixedSections(std::span<const uint8_t> bytes);
void writeD2sFixedSections(Bytes &bytes, const D2sFixedSections &sections);
} // namespace d2x