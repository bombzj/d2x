#pragma once
#include "core/math.hpp"
#include <cstdint>

namespace d2x {
// Native path facing to the scene's original player sprites / world heading.
int nativePlayerSpriteDirection(uint8_t facing, int count, bool remotePlayer);
Vec nativeFacingVector(uint8_t facing);
} // namespace d2x
