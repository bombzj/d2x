#pragma once
#include <cstdint>
#include <optional>

namespace d2x {
// SkillItem::pSpell03: one stat list per original potion state. The combined
// remaining value is divided by the combined remaining frames, in 8.8 units.
struct TimedRestoration { uint64_t until{}, next{}; int64_t perFrame{}; };
std::optional<TimedRestoration> combineRestoration(const TimedRestoration *, uint64_t now,
                                                  uint64_t frames, int64_t amount);
int64_t advanceRestorationFrame(TimedRestoration &, uint64_t now);
} // namespace d2x
