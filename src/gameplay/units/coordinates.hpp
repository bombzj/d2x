#pragma once
#include "core/math.hpp"

namespace d2x {
// Player/monster actors publish precise feet with the drawing half-cell removed.
// Restore it before reading a native integer subtile. The resulting integer
// is also the display-foot coordinate for standing at that cell's center.
// Missile motion and grid-local positions already include the half-cell and
// must not use this conversion.
inline Vec nativeFeetCell(Vec position) {
    return {std::floor(position.x+.5f),std::floor(position.y+.5f)};
}
}
