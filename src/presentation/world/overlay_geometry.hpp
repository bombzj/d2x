#pragma once
#include "core/math.hpp"
#include <array>
#include <cstddef>

namespace d2x {
// D2Client 6A91C and 6B817 use path client Y - (camera Y - 8).
// This display origin is shared by static and dynamic units.
inline constexpr Vec unitScreenOffset{0, 8};

// Retail 1.13c D2Client 6B463 / 6AEE0: the native overlay height index
// selects Height1..4; -1 adds 75, other out-of-range indices add nothing.
inline Vec overlayScreenOffset(Vec offset, const std::array<int, 4> &heights, int index) {
    const int height = index == -1 ? 75 : index >= 0 && index < 4 ? heights[std::size_t(index)] : 0;
    return offset + unitScreenOffset + Vec{0, float(height)};
}
} // namespace d2x
