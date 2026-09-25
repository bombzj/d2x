#pragma once
#include "primitives.hpp"

namespace d2x {
// 800borderframe.dc6 is 400 x 558 per side. Preserve the source aspect ratio
// and anchor the two panels to the viewport edges on a wider game canvas.
inline constexpr float classicPanelScale = float(H - HUD) / 558.f;
inline Rectangle classicSideBounds(bool right) {
    return {right ? W - 400 * classicPanelScale : 0, 0,
            400 * classicPanelScale, 558 * classicPanelScale};
}
inline Rectangle classicPanelBounds(bool right, float top = 60) {
    auto side = classicSideBounds(right);
    return {side.x + (right ? 0 : 80 * classicPanelScale), top * classicPanelScale,
            320 * classicPanelScale, 432 * classicPanelScale};
}
} // namespace d2x
