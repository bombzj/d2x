#pragma once
#include "primitives.hpp"
#include "presentation/input.hpp"

namespace d2x {
class PaletteBlendView;
inline bool classicButtonHovered(Rectangle bounds, Vec mouse) {
    return CheckCollisionPointRec(rv(mouse), bounds);
}

enum class ClassicButtonResult { None, Captured, Activated };

struct ClassicButtonGesture {
    bool pressed = false;

    ClassicButtonResult handle(Rectangle bounds, const FrameInput &input) {
        if (!input.focused) {
            pressed = false;
            return ClassicButtonResult::None;
        }
        const bool hovered = input.insideViewport && classicButtonHovered(bounds, input.mouse);
        if (pressed && input.leftReleased) {
            pressed = false;
            return hovered ? ClassicButtonResult::Activated : ClassicButtonResult::Captured;
        }
        if (input.leftPressed) pressed = hovered;
        else if (!input.leftHeld) pressed = false;
        return pressed ? ClassicButtonResult::Captured : ClassicButtonResult::None;
    }
};

void drawClassicSpriteButton(const GpuAnimation &art, int normalFrame, Rectangle imageBounds,
    Rectangle hitBounds, Vec mouse, bool pressed, const PaletteBlendView *blend);
} // namespace d2x
