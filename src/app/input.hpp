#pragma once
#include "presentation/input.hpp"

namespace d2x {
struct Viewport {
    float scale = 1;
    Vec offset;
};
Viewport currentViewport();
FrameInput pollInput(const Viewport &viewport, const ControlBindings &);
} // namespace d2x
