#include "gameplay/units/restoration.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
std::optional<TimedRestoration> combineRestoration(const TimedRestoration *previous, uint64_t now,
                                                  uint64_t frames, int64_t amount) {
    if (!frames || amount <= 0 || frames > UINT64_MAX - now) return {};
    const uint64_t remaining = previous && previous->until > now ? previous->until - now : 0;
    if (remaining > UINT64_MAX - now - frames || frames + remaining > uint64_t(INT64_MAX)) return {};
    if (remaining && (previous->perFrame < 0 ||
        uint64_t(previous->perFrame) > uint64_t(INT64_MAX - amount) / remaining)) return {};
    const int64_t total = amount + (remaining ? int64_t(remaining) * previous->perFrame : 0);
    return TimedRestoration{now + frames + remaining, remaining ? previous->next : now,
                            total / int64_t(frames + remaining)};
}
int64_t advanceRestorationFrame(TimedRestoration &effect, uint64_t now) {
    if (now < effect.next || effect.next >= effect.until) return 0;
    const auto last = std::min(now,effect.until - 1);
    const auto frames = last - effect.next + 1;
    effect.next = last + 1;
    return int64_t(frames) * effect.perFrame;
}
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
