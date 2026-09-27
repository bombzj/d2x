#include "scene_view.hpp"
#include <algorithm>

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
void SceneView::drawShrineOverlays() const {
    auto drawLayer = [&](const SceneAssets::OverlayArt &visual, Vec at, int heightIndex) {
        if (visual.frames <= 0 || visual.fps <= 0) return;
        const int frameIndex = int(view_.animationTime * visual.fps) % visual.frames;
        const auto *frame = visual.animation.frame(0, frameIndex);
        if (!frame || !frame->texture.id) return;
        at = at + visual.offset;
        at.y += visual.heights[size_t(heightIndex)];
        if (visual.trans == 3) softAdditiveSprite(frame, at);
        else sprite(frame, at);
    };
    auto drawPair = [&](const std::array<SceneAssets::OverlayArt, 2> &art, Vec at,
                        int heightIndex) {
        // States.txt overlay2 is the shimmer behind overlay1's shrine symbol.
        drawLayer(art[1], at, heightIndex);
        drawLayer(art[0], at, heightIndex);
    };
    for (const auto &[region, offset] : session_.sceneRegions())
        for (const auto &object : session_.regions()[region].objects) {
            if (object.interaction != Interaction::Shrine || !visible(object)) continue;
            const auto found = assets_.shrineOverlays.find(object.shrineCode);
            if (found == assets_.shrineOverlays.end()) continue;
            // D2MOO UNITS_GetOverlayHeight returns 0 for an object.
            drawPair(found->second, screen(object.pos + offset), 0);
        }
    if (session_.state().player.dead) return;
    for (auto it = session_.shrineStatuses().rbegin(); it != session_.shrineStatuses().rend(); ++it) {
        const auto found = assets_.shrineOverlays.find(it->code);
        if (found == assets_.shrineOverlays.end()) continue;
        const auto effects = session_.state().player.combatEffects.entries();
        if (std::none_of(effects.begin(), effects.end(), [&](const ActiveCombatEffect &effect) {
                return effect.handle == it->stateEffect && effect.activeAt(session_.state().frame);
            })) continue;
        // The same native function returns 1 for a player.
        drawPair(found->second, screen(session_.state().player.pos), 1);
        break;
    }
}
void SceneView::drawCombatStateOverlays(const CombatEffectSet &effects, Vec at, int height, bool back) const {
    for (const auto &effect : effects.entries()) {
        if (!effect.activeAt(session_.state().frame)) continue;
        const auto found = assets_.combatStateOverlays.find(effect.spec.state.id);
        if (found == assets_.combatStateOverlays.end()) continue;
        const auto &art = found->second[back ? 1 : 0];
        if (art.frames <= 0 || art.fps <= 0) continue;
        const auto *frame = art.animation.frame(0, int(view_.animationTime * art.fps) % art.frames);
        const Vec position = at + art.offset + Vec{0, float(art.heights[size_t(std::clamp(height, 0, 3))])};
        if (art.trans == 3) softAdditiveSprite(frame, position);
        else sprite(frame, position);
    }
}
} // namespace d2x
