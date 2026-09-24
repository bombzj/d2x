#pragma once
#include "primitives.hpp"

namespace d2x {
// One coordinate system for drawing and hit testing the original 800-pixel panel.
// Native DC6 panel frames are bottom-aligned; the globes rise above the 55px strip.
inline constexpr float hudScale = W / 800.f;
inline Rectangle hudRect(float x, float fromBottom, float width, float height) {
    return {x * hudScale, H - fromBottom * hudScale, width * hudScale, height * hudScale};
}
inline Rectangle hudSkillSlot(bool right) {
    return hudRect(right ? 635 : 117, 48, 48, 48);
}
inline Rectangle hudGlobe(bool mana) {
    return hudRect(mana ? 690 : 30, mana ? 92 : 93, 80, 80);
}
inline Rectangle hudRunButton() {
    return hudRect(255, 30, 16, 20);
}
inline Rectangle hudStamina() {
    return hudRect(273, 28, 102, 19);
}
inline Rectangle hudExperience() {
    return hudRect(256, 39, 120, 4);
}
inline Rectangle hudCharacterButton() {
    return hudRect(206, 39, 30, 30);
}
inline Rectangle hudSkillTreeButton() {
    return hudRect(563, 39, 30, 30);
}
inline Rectangle hudMenuButton() {
    return hudRect(393, 39, 16, 32);
}
inline Rectangle hudPickerSlot(bool right, int index, int count) {
    constexpr float side = 45;
    constexpr int columns = 6;
    int rows = (count + columns - 1) / columns;
    float left = right ? W - 20 - columns * side : 20;
    return {left + (index % columns) * side,
            float(H - HUD) - rows * side + (index / columns) * side,
            side, side};
}
inline bool hudSurface(Vec mouse) {
    return mouse.y >= H - HUD || CheckCollisionPointRec(rv(mouse), hudRect(0, 104, 117, 104)) ||
           CheckCollisionPointRec(rv(mouse), hudRect(683, 104, 117, 104));
}
} // namespace d2x
