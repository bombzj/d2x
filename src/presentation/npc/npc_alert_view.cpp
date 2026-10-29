#include "presentation/scene_view.hpp"
#include "presentation/world/overlay_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SceneView::drawNpcAlert(EntityId npc, Vec at, bool back) const {
    const auto *entry = npcScene_.npc(npc);
    const auto &overlay = assets_.npcAlert;
    if (!entry || !entry->questAlert || overlay.preDraw != back || overlay.frames <= 0 || overlay.fps <= 0) return;
    const int height = entry->overlayHeight;
    const double ticks = std::floor(std::max(0.0,double(view_.animationTime))*25.0+1e-5);
    const int frame = int(ticks*overlay.fps/25.0) % overlay.frames;
    at = at + overlayScreenOffset(overlay.offset,overlay.heights,height);
    // Preserve original DCC anchor, MonStats2 height and black-alpha blending.
    if (overlay.trans == 3) BeginBlendMode(BLEND_ADDITIVE);
    sprite(overlay.animation.frame(0, frame), at);
    if (overlay.trans == 3) EndBlendMode();
}
} // namespace d2x
