#include "scene_direction.hpp"
#include "presentation/graphics/primitives.hpp"
#include <cmath>

namespace d2x {
namespace {
constexpr int southClockwise8[] = {4, 0, 5, 1, 6, 2, 7, 3};
constexpr int southClockwise16[] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};
}
int nativePlayerSpriteDirection(uint8_t facing, int count, bool remotePlayer) {
    // D2Client 1.13c 6D960/6D220: other players use eight logical directions.
    // For a 16-direction COF, #10057 lowers the raw sector before #10020
    // selects the COF row. Recovering the DCC index uses the same adjusted heading.
    if (remotePlayer && count == 16) facing = uint8_t(facing & 0x38);
    // D2CMP 1.13c RVA 0x12240 / 0x1E4D8.
    if (count == 8) return southClockwise8[((facing + 4) & 63) / 8];
    if (count == 16) return southClockwise16[((facing + 2) & 63) / 4];
    return direction(nativeFacingVector(facing), count);
}
Vec nativeFacingVector(uint8_t facing) {
    const float angle = (float(facing & 63) - 7.5f) * 2.f * pi / 64.f;
    return {-std::sin(angle), std::cos(angle)};
}
} // namespace d2x
