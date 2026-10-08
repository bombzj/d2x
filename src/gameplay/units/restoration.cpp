#include "gameplay/units/restoration.hpp"
#include <algorithm>

namespace d2x {
namespace {
template<class Apply>
void advanceRestoration(std::deque<ResourceRestoration> &queue, float dt, Apply apply) {
    float remaining = dt;
    while (!queue.empty() && remaining > 0) {
        auto &effect = queue.front();
        const float amount = std::min(effect.remaining, remaining * effect.rate);
        remaining -= amount / effect.rate;
        effect.remaining -= amount;
        apply(amount);
        if (effect.remaining <= .0001f) queue.pop_front();
    }
}
} // namespace
void restoreResource(std::deque<ResourceRestoration> &queue, float &value, float maximum, float dt) {
    advanceRestoration(queue, dt, [&](float amount) { value = std::min(maximum, value + amount); });
    // Reaching full wastes unused restoration, matching the existing consumable rule.
    if (value >= maximum) queue.clear();
}
void restoreRegeneratingLife(std::deque<ResourceRestoration> &queue, float &life, int maximum,
                            int baseLife, int replenishLife, float dt) {
    const int regen = baseLife * 256 / 2000 + replenishLife;
    float delta = float(regen) / 256.f * 25.f * dt;
    // Start with natural regeneration, then add each queued amount. Computing
    // healing separately would change the original floating-point addition order.
    advanceRestoration(queue, dt, [&](float amount) { delta += amount; });
    life = std::min(float(maximum), life + delta);
    if (life >= maximum) queue.clear();
}
} // namespace d2x
