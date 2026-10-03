#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::drawShrineOverlays(int code, Vec at, int heightIndex, bool back) const {
    const auto found = assets_.shrineOverlays.find(code);
    if (found == assets_.shrineOverlays.end()) return;
    auto drawLayer = [&](const SceneAssets::OverlayArt &visual, Vec at, int heightIndex) {
        if (visual.frames <= 0 || visual.fps <= 0 || visual.preDraw != back) return;
        const int frameIndex = int(view_.animationTime * visual.fps) % visual.frames;
        const auto *frame = visual.animation.frame(0, frameIndex);
        if (!frame || !frame->texture.id) return;
        at = at + visual.offset;
        at.y += visual.heights[size_t(heightIndex)];
        if (visual.trans == 3) softAdditiveSprite(frame, at);
        else sprite(frame, at);
    };
    // States secondary layer first; each original Overlay.PreDraw chooses
    // whether it belongs behind or in front of this same world unit.
    drawLayer(found->second[1], at, heightIndex);
    drawLayer(found->second[0], at, heightIndex);
}
void SceneView::drawPlayerShrineOverlay(Vec at, bool back) const {
    if (session_.state().player.actions.dead) return;
    for (auto it = session_.shrineStatuses().rbegin(); it != session_.shrineStatuses().rend(); ++it) {
        const auto found = assets_.shrineOverlays.find(it->code);
        if (found == assets_.shrineOverlays.end()) continue;
        const auto effects = session_.state().player.combatEffects.entries();
        if (std::none_of(effects.begin(), effects.end(), [&](const ActiveCombatEffect &effect) {
                return effect.handle == it->stateEffect && effect.activeAt(session_.state().frame);
            })) continue;
        // The same native function returns 1 for a player.
        drawShrineOverlays(it->code, at, 1, back);
        break;
    }
}
void SceneView::drawCombatStateOverlays(const CombatEffectSet &effects, Vec at, int height, bool back) const {
    for (const auto &effect : effects.entries()) {
        if (!effect.activeAt(session_.state().frame)) continue;
        const auto found = assets_.combatStateOverlays.find(effect.spec.state.id);
        if (found == assets_.combatStateOverlays.end()) continue;
        for (int layer : {1, 0}) {
            const auto &art = found->second[size_t(layer)];
            if (art.frames <= 0 || art.fps <= 0 || art.preDraw != back) continue;
            const bool aura = effect.spec.stacking == EffectStacking::AuraLevel;
            const float elapsed = aura ? float(session_.state().frame - effect.startedAt) / 25.f : view_.animationTime;
            const auto *frame = art.animation.frame(0, int(elapsed * art.fps) % art.frames);
            const Vec position = at + art.offset + Vec{0, float(art.heights[size_t(std::clamp(height, 0, 3))])};
            if (art.trans == 3) paletteBlend_.draw(frame, position);
            else sprite(frame, position);
        }
    }
}
} // namespace d2x
