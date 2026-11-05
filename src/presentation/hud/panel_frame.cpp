#include "presentation/scene_view.hpp"
#include "classic_panel.hpp"

namespace d2x {
float classicPanelScale() { return 1.f; }
Rectangle classicSideBounds(bool right) {
    return {right ? W - 400 * classicPanelScale() : 0, float(std::max(0, (H - 600) / 2)),
            400 * classicPanelScale(), 558 * classicPanelScale()};
}
Rectangle classicPanelBounds(bool right, float top) {
    auto side = classicSideBounds(right);
    return {side.x + (right ? 0 : 80 * classicPanelScale()), side.y + top * classicPanelScale(),
            320 * classicPanelScale(), 432 * classicPanelScale()};
}
Rectangle classicInsetPanelBounds(bool right) {
    // Retail character/skill/quest backgrounds draw their first 256-pixel
    // cell at bottom row H-60-224 = 316 in 800x600. DC6's bottom row is
    // inclusive, so the top is 316-256+1 = 61, not the reference UI's 64.
    return classicPanelBounds(right, 61);
}
void SceneView::drawPanelFrame(bool right) const {
    const auto &border = assets_.waypointBorder;
    const auto side = classicSideBounds(right);
    DrawRectangleRec(side, BLACK);
    // Native DC6 pieces are not a rectangular tile sheet; the middle consists
    // only of the outer vertical border. See OpenDiablo2 d2ui/frame.go.
    constexpr Vec positions[] = {{0, 0}, {256, 0}, {0, 256}, {0, 487}, {256, 487},
                                 {0, 0}, {145, 0}, {314, 256}, {145, 487}, {0, 487}};
    const int first = right ? 5 : 0;
    for (int index = first; index < first + 5; ++index) {
        const auto *image = border.frame(0, index);
        if (!image) continue;
        const auto &texture = image->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
            {side.x + positions[index].x * classicPanelScale(),
             side.y + positions[index].y * classicPanelScale(),
             texture.width * classicPanelScale(), texture.height * classicPanelScale()}, {0, 0}, 0, WHITE);
    }
}
} // namespace d2x
