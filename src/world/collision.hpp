#pragma once
#include <cstdint>

namespace d2x {
struct MissileCollisionRule {
    uint16_t mask = 0;
    int size = 0;
};
struct MovementCollisionRule {
    uint16_t mask = 0x1c09; // WALL | NOPLAYER | OBJECT | DOOR | NO_PATH.
    int size = 1; // Point for spatial queries; dynamic path patterns use cross/square.
};
inline constexpr MovementCollisionRule playerMovement{0x1c09, 2};
} // namespace d2x
