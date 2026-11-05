#pragma once
#include "presentation/graphics/primitives.hpp"

namespace d2x {
// 800borderframe.dc6 is 400 x 558 per side. Preserve the source aspect ratio
// and anchor the two panels to the viewport edges on a wider game canvas.
// Center the original 600-pixel layout, not the 558-pixel artwork, so the
// native panel placement and HUD overlap remain unchanged at 800x600.
float classicPanelScale();
Rectangle classicSideBounds(bool right);
Rectangle classicPanelBounds(bool right, float top = 60);
Rectangle classicInsetPanelBounds(bool right);
} // namespace d2x
