#pragma once
#include "core/math.hpp"
#include <deque>

namespace d2x {
struct PlayerMovement {
    Vec pos, previous, look{1, 0};
    std::deque<Vec> route;
    bool running = false, runningNow = false, moving = false;
};
} // namespace d2x
