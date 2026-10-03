#pragma once
#include "core/id.hpp"
#include "core/math.hpp"

namespace d2x {
// Authority-bound continuous control, independent of keys, devices and frame time.
struct PlayerFrameInput {
    EntityId actor;
    Vec direction;
    bool forceRun = false;
};
} // namespace d2x
