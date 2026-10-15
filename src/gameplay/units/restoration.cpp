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
} // namespace d2x
