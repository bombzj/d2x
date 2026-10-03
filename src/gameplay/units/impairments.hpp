#pragma once
#include "gameplay/units/ailments.hpp"

namespace d2x {
// Optional borrowed timers; absent freeze/stun capabilities are not fabricated.
struct ImpairmentView {
    float *chill = nullptr, *freeze = nullptr, *stun = nullptr;
    bool *freezeActive = nullptr;
    WebSlowView webSlow;
};
enum class WebSlowExpiry { RetainMetadata, ClearMetadata };

// Advance only at the caller's original phase, after its original early exits.
// Freeze's native bit is refreshed even when its remaining timer is already zero.
void advanceImpairments(const ImpairmentView &view, float dt, WebSlowExpiry webExpiry);
} // namespace d2x
