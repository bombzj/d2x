#include "gameplay/units/impairments.hpp"
#include <algorithm>

namespace d2x {
void advanceImpairments(const ImpairmentView &view, float dt, WebSlowExpiry webExpiry) {
    if (view.chill) *view.chill = std::max(0.f, *view.chill - dt);
    if (view.freeze) {
        *view.freeze = std::max(0.f, *view.freeze - dt);
        if (view.freezeActive) *view.freezeActive = *view.freeze > 0;
    }
    if (view.stun) *view.stun = std::max(0.f, *view.stun - dt);
    if (view.webSlow.remaining) {
        *view.webSlow.remaining = std::max(0.f, *view.webSlow.remaining - dt);
        if (*view.webSlow.remaining == 0 && webExpiry == WebSlowExpiry::ClearMetadata) {
            if (view.webSlow.percent) *view.webSlow.percent = 0;
            if (view.webSlow.source) *view.webSlow.source = {};
        }
    }
}
} // namespace d2x
