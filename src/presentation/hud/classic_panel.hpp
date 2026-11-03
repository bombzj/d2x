#pragma once
#include "presentation/graphics/primitives.hpp"

namespace d2x {
// 800borderframe.dc6 is 400 x 558 per side. Preserve the source aspect ratio
// and anchor the two panels to the viewport edges on a wider game canvas.
// 558 is the artwork height, not the reference viewport height. At 800x600
// the original panel must be drawn 1:1, including its overlap with the HUD.
inline float classicPanelScale() { return std::min(float(W) / 800.f, float(H) / 600.f); }
inline Rectangle classicSideBounds(bool right) {
    return {right ? W - 400 * classicPanelScale() : 0, 0,
            400 * classicPanelScale(), 558 * classicPanelScale()};
}
inline Rectangle classicPanelBounds(bool right, float top = 60) {
    auto side = classicSideBounds(right);
    return {side.x + (right ? 0 : 80 * classicPanelScale()), top * classicPanelScale(),
            320 * classicPanelScale(), 432 * classicPanelScale()};
}
inline Rectangle classicInsetPanelBounds(bool right) {
    // Retail character/skill/quest backgrounds draw their first 256-pixel
    // cell at bottom row H-60-224 = 316 in 800x600. DC6's bottom row is
    // inclusive, so the top is 316-256+1 = 61, not the reference UI's 64.
    return classicPanelBounds(right, 61);
}
} // namespace d2x
