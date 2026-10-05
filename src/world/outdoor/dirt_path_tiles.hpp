#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

// Shared dirt-road topology and rasterization for all map generation callers.
// Native algorithm constants from D2MOO DrlgOutdoors/DrlgGrid, MIT:
// docs/licenses/D2MOO.txt. Tile contents and collision still come from the MPQ.
namespace d2x {
namespace detail {
inline constexpr std::array<uint8_t, 256> dirtPathTiles{
    0,0,16,16,0,0,16,16,14,14,6,19,14,14,6,19,
    15,15,5,5,15,15,21,21,8,8,10,38,8,8,40,20,
    0,0,16,16,0,0,16,16,14,14,6,19,14,14,6,19,
    15,15,5,5,15,15,21,21,8,8,10,38,8,8,40,20,
    13,13,7,7,13,13,13,7,4,4,11,37,4,4,11,43,
    3,3,12,12,3,3,39,39,9,9,2,43,9,9,44,26,
    13,13,7,7,13,13,13,7,23,23,41,17,23,23,41,17,
    3,3,12,12,3,3,39,39,42,42,46,42,42,42,33,31,
    0,0,16,16,0,0,16,16,14,14,6,19,14,14,6,19,
    15,15,5,5,15,15,21,21,8,8,10,38,8,8,35,20,
    0,0,16,16,0,0,16,16,14,14,6,19,14,14,6,19,
    15,15,5,5,15,15,21,21,8,8,10,38,8,8,40,20,
    13,13,7,7,13,13,13,7,4,4,11,37,4,4,11,37,
    18,18,35,35,18,18,22,22,36,36,45,34,36,36,28,29,
    13,13,7,7,13,13,13,7,23,23,41,17,23,23,41,17,
    18,18,35,35,18,18,22,22,24,24,25,32,24,24,30,1};
}
inline bool isDirtPathSequence(unsigned sequence) {
    return sequence != 0 && std::find(detail::dirtPathTiles.begin(),
        detail::dirtPathTiles.end(), sequence) != detail::dirtPathTiles.end();
}
inline constexpr uint32_t dirtPathFloor(uint8_t mask) {
    const auto sequence = detail::dirtPathTiles[mask];
    return sequence ? (uint32_t(sequence) << 8) | 0x82u : 0u;
}
template<class Sample>
uint8_t dirtPathMask(int x, int y, const Sample &sample) {
    unsigned mask = 0;
    // Column-major neighborhood, reversed Y; centre omitted.
    for (int index = 8; index >= 0; --index)
        if (index != 4)
            mask = (mask << 1) | unsigned(sample(x + index / 3 - 1, y + 1 - index % 3) != 0);
    return uint8_t(mask);
}
template<class Stamp>
void rasterizeDirtPath(int x, int y, int toX, int toY, const Stamp &stamp) {
    const int dx = std::abs(toX - x), dy = std::abs(toY - y);
    const int sx = toX >= x ? 1 : -1, sy = toY >= y ? 1 : -1;
    const bool horizontal = dx >= dy;
    const int length = horizontal ? dx : dy;
    int error = 0;
    for (int step = 0; step <= length; ++step) {
        stamp(x, y);
        // The original width-two brush extends toward the positive minor axis.
        stamp(x + !horizontal, y + horizontal);
        if (horizontal) {
            x += sx; error += dy;
            if (error > dx) { y += sy; error -= dx; }
        } else {
            y += sy; error += dx;
            if (error > dy) { x += sx; error -= dy; }
        }
    }
}
} // namespace d2x
