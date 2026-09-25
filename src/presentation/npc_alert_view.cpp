#include "scene_view.hpp"

namespace d2x {
void SceneView::drawNpcAlerts() const {
    const auto &overlay = assets_.npcAlert;
    const int frame = int(view_.animationTime * overlay.fps) % overlay.frames;
    for (const auto &npc : session_.region().objects) {
        if (!visible(npc) || !session_.npcQuestAlert(npc)) continue;
        const auto *record = session_.monsterContent().find(npc.npcClass);
        if (!record) continue;
        const int height = record->overlayHeight;
        Vec at = screen(npc.pos) + overlay.offset;
        at.y += height >= 1 && height <= 4 ? overlay.heights[size_t(height - 1)] : 75;
        // Overlay Trans=3 uses black-alpha blending. Keep the original DCC's
        // anchor and MonStats2 height selection for every NPC.
        if (overlay.trans == 3) BeginBlendMode(BLEND_ADDITIVE);
        sprite(overlay.animation.frame(0, frame), at);
        if (overlay.trans == 3) EndBlendMode();
    }
}
} // namespace d2x
