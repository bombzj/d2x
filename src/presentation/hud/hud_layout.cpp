#include "hud_layout.hpp"

namespace d2x {
float hudScale() { return float(W) / 800.f; }

float hudTextScale() { return 1.f; }

Rectangle hudRect(float x, float fromBottom, float width, float height) {
    return {x * hudScale(), H - fromBottom * hudScale(), width * hudScale(), height * hudScale()};
}

Rectangle hudSkillSlot(bool right) {
    return hudRect(right ? 635 : 117, 48, 48, 48);
}

Rectangle hudGlobe(bool mana) {
    return hudRect(mana ? 690 : 30, mana ? 92 : 93, 80, 80);
}

Rectangle hudRunButton() {
    return hudRect(255, 30, 16, 20);
}

Rectangle hudStamina() {
    return hudRect(273, 28, 102, 19);
}

Rectangle hudExperience() {
    return hudRect(256, 39, 120, 4);
}

Rectangle hudBeltSlot(int column, int row) {
    return hudRect(425 + 31 * column, 41 + 32 * row, 29, 29);
}

Rectangle hudCharacterButton() {
    // D2Client 4FF10 tests the strict interior of (206,H-42)..(240,H-8).
    return hudRect(207, 41, 33, 33);
}

Rectangle hudSkillTreeButton() {
    // D2Client 4FE70 uses the same hit area around the right button.
    return hudRect(564, 41, 33, 33);
}

Rectangle hudMenuButton() {
    return hudRect(393, 39, 16, 32);
}

Rectangle hudMiniPanel(const Sprite &image) {
    return hudRect(325, 49 + image.texture.height,
                   float(image.texture.width), float(image.texture.height));
}

Rectangle hudMiniButton(const Sprite &image, int index) {
    return hudRect(328 + index * (image.texture.width + 1), 52 + image.texture.height,
                   float(image.texture.width), float(image.texture.height));
}

Rectangle hudPickerSlot(bool right, int column, int row) {
    constexpr float side = 48;
    return hudRect(right ? 720 - (column + 1) * side : 90 + column * side,
                   135 + row * side, side, side);
}

bool hudSurface(Vec mouse) {
    return mouse.y >= H - HUD || CheckCollisionPointRec(rv(mouse), hudRect(0, 104, 117, 104)) ||
           CheckCollisionPointRec(rv(mouse), hudRect(683, 104, 117, 104));
}
} // namespace d2x
