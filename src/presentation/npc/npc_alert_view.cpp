#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawNpcAlert(EntityId npc, Vec at, bool back) const {
    const auto *entry = npcScene_.npc(npc);
    const auto &overlay = assets_.npcAlert;
    if (!entry || !entry->questAlert || overlay.preDraw != back || overlay.frames <= 0 || overlay.fps <= 0) return;
    const int height = entry->overlayHeight;
    if (height < 0 || height >= int(overlay.heights.size())) return;
    const int frame = int(view_.animationTime * overlay.fps) % overlay.frames;
    at = at + overlay.offset;
    at.y += overlay.heights[size_t(height)];
    // Preserve original DCC anchor, MonStats2 height and black-alpha blending.
    if (overlay.trans == 3) BeginBlendMode(BLEND_ADDITIVE);
    sprite(overlay.animation.frame(0, frame), at);
    if (overlay.trans == 3) EndBlendMode();
}
} // namespace d2x
