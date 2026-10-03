#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "world/region.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
const LevelExit *SceneView::exitAt(Vec mouse) const {
    if (hudSurface(mouse) || view_.blocksWorld() || !CheckCollisionPointRec(rv(mouse), worldViewport()))
        return nullptr;
    for (const auto &exit : session_.region().exits) {
        auto p = screen(exit.position);
        const auto &r = exit.selection;
        Rectangle bounds{p.x + r.selectX * view_.zoom, p.y + r.selectY * view_.zoom,
                         r.selectWidth * view_.zoom, r.selectHeight * view_.zoom};
        if (CheckCollisionPointRec(rv(mouse), bounds))
            return &exit;
    }
    return nullptr;
}
void SceneView::drawExitHint(Vec mouse) const {
    if (view_.inventory.drag)
        return;
    if (const auto *exit = exitAt(mouse)) {
        auto p = screen(exit->position);
        std::string text = exit->name;
        if (!exit->enabled)
            text += " (not available yet)";
        int width = painter_.measure(text, 14);
        DrawRectangle(int(p.x - width / 2 - 8), int(p.y - 56), width + 16, 24, {0, 0, 0, 210});
        painter_.label(text, int(p.x - width / 2), int(p.y - 51), 14, gold);
    }
}
void SceneView::drawObjectHint(Vec mouse) const {
    if (view_.inventory.drag || view_.blocksWorld() || hudSurface(mouse) ||
        !CheckCollisionPointRec(rv(mouse), worldViewport()))
        return;
    const auto *object = objectAt(mouse);
    if (!object || object->name.empty() || exitAt(mouse))
        return;
    const Vec p = objectScreen(*object);
    std::string label = object->name;
    if (object->chest && object->chest->locked) {
        const auto &strings = session_.content().itemStrings;
        if (auto name = strings.find("lockedchest"); name != strings.end()) label = name->second;
    }
    // OpenDiablo2 HUD uses Font16; uncolored Object.Label text defaults to white.
    const int width = painter_.measure(label, 16);
    DrawRectangle(int(p.x - width / 2 - 8), int(p.y - 70), width + 16, 24, {0, 0, 0, 210});
    painter_.label(label, int(p.x - width / 2), int(p.y - 65), 16, WHITE);
}
} // namespace d2x
