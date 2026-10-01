#include "presentation/scene_view.hpp"
#include "classic_panel.hpp"

namespace d2x {
void SceneView::drawPanelFrame(bool right) const {
    const auto side = classicSideBounds(right);
    // Native DC6 pieces are not a rectangular tile sheet; the middle consists
    // only of the outer vertical border. See OpenDiablo2 d2ui/frame.go.
    constexpr Vec positions[] = {{0, 0}, {256, 0}, {0, 256}, {0, 487}, {256, 487},
                                 {0, 0}, {145, 0}, {314, 256}, {145, 487}, {0, 487}};
    const int first = right ? 5 : 0;
    for (int index = first; index < first + 5; ++index) {
        const auto *image = assets_.waypointBorder.frame(0, index);
        if (!image) continue;
        const auto &texture = image->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
            {side.x + positions[index].x * classicPanelScale,
             positions[index].y * classicPanelScale,
             texture.width * classicPanelScale, texture.height * classicPanelScale}, {0, 0}, 0, WHITE);
    }
}
} // namespace d2x
