#pragma once
#include <deque>
#include <cstdint>
#include <optional>

namespace d2x {
struct ResourceRestoration {
    float remaining = 0, rate = 0;
};
// SkillItem::pSpell03: one stat list per original potion state. The combined
// remaining value is divided by the combined remaining frames, in 8.8 units.
struct TimedRestoration { uint64_t until{}, next{}; int64_t perFrame{}; };
std::optional<TimedRestoration> combineRestoration(const TimedRestoration *, uint64_t now,
                                                  uint64_t frames, int64_t amount);
int64_t advanceRestorationFrame(TimedRestoration &, uint64_t now);
// Each queue consumes the elapsed time in order with the caller's current cap.
void restoreResource(std::deque<ResourceRestoration> &queue, float &value, float maximum, float dt);
// Native companion recovery uses unequipped base life for its /2000 rate,
// then adds replenish life and queued healing before the final equipped cap.
void restoreRegeneratingLife(std::deque<ResourceRestoration> &queue, float &life, int maximum,
                            int baseLife, int replenishLife, float dt);
} // namespace d2x
