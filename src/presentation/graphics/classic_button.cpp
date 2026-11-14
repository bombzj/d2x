#include "classic_button.hpp"
#include "presentation/world/palette_blend_view.hpp"

namespace d2x {
void drawClassicSpriteButton(const GpuAnimation &art, int normalFrame, Rectangle imageBounds,
    Rectangle hitBounds, Vec mouse, bool pressed, const PaletteBlendView *blend) {
    const bool hovered = classicButtonHovered(hitBounds, mouse);
    const auto *frame = art.frame(0, normalFrame + pressed);
    if (!frame) return;
    if (hovered && !pressed && blend && frame->indexedTexture.id) {
        blend->drawHighlight(frame, imageBounds);
        return;
    }
    const auto &texture = frame->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, imageBounds, {}, 0, WHITE);
}
} // namespace d2x
