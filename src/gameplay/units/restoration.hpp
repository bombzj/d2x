#pragma once
#include <deque>

namespace d2x {
struct ResourceRestoration {
    float remaining = 0, rate = 0;
};
// Each queue consumes the elapsed time in order with the caller's current cap.
void restoreResource(std::deque<ResourceRestoration> &queue, float &value, float maximum, float dt);
// Native companion recovery uses unequipped base life for its /2000 rate,
// then adds replenish life and queued healing before the final equipped cap.
void restoreRegeneratingLife(std::deque<ResourceRestoration> &queue, float &life, int maximum,
                            int baseLife, int replenishLife, float dt);
} // namespace d2x
