#include "hud_layout.hpp"

namespace d2x {
float hudScale() { return 1.f; }

float hudTextScale() { return 1.f; }

Rectangle hudRect(float x, float fromBottom, float width, float height, HudAnchor anchor) {
    const int extra = W - 800;
    const int offset = anchor == HudAnchor::Left ? 0 : anchor == HudAnchor::Right ? extra : extra / 2;
    return {x + offset, H - fromBottom, width, height};
}

Rectangle hudSkillSlot(bool right) {
    return hudRect(right ? 635 : 117, 48, 48, 48, right ? HudAnchor::Right : HudAnchor::Left);
}

Rectangle hudGlobe(bool mana) {
    return hudRect(mana ? 690 : 30, mana ? 92 : 93, 80, 80, mana ? HudAnchor::Right : HudAnchor::Left);
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
    // D2Client 26560: inclusive W/2-8..W/2+5, H-39..H-13.
    return hudRect(392, 39, 14, 27);
}

Rectangle hudMenuButtonArt(const Sprite &image) {
    // 26880 draws the native 15x24 cell at W/2-8, baseline H-16.
    return hudRect(392, 16 + image.texture.height, float(image.texture.width), float(image.texture.height));
}

Rectangle hudMiniPanel(const Sprite &image, int offset) {
    // D2Client 64A64..64A84: W/2-87, baseline H-47.
    // 649F9 uses a two-pixel left border when shifted right, otherwise three.
    return hudRect(float(313 + offset + (offset > 0 ? 1 : 0)), 47 + image.texture.height,
                   float(image.texture.width), float(image.texture.height));
}

Rectangle hudMiniButton(const Sprite &image, int index, int offset) {
    // 63000: W/2-84, 21-pixel stride, baseline H-50.
    return hudRect(float(316 + offset + index * (image.texture.width + 1)), 50 + image.texture.height,
                   float(image.texture.width), float(image.texture.height));
}

Rectangle hudPickerSlot(bool right, int column, int row) {
    constexpr float side = 48;
    return hudRect(right ? 720 - (column + 1) * side : 90 + column * side,
                   135 + row * side, side, side, right ? HudAnchor::Right : HudAnchor::Left);
}

bool hudSurface(Vec mouse) {
    return mouse.y >= H - HUD || CheckCollisionPointRec(rv(mouse), hudRect(0, 104, 117, 104, HudAnchor::Left)) ||
           CheckCollisionPointRec(rv(mouse), hudRect(683, 104, 117, 104, HudAnchor::Right));
}
} // namespace d2x
