#include "scene_view.hpp"

namespace d2x {
const LevelExit *SceneView::exitAt(Vec mouse) const {
    if (hudSurface(mouse) || view_.blocksWorld())
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
    if (view_.inventory.open || view_.inventory.drag)
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
    if (view_.inventory.open || view_.inventory.drag || view_.blocksWorld() || mouse.y >= H - HUD)
        return;
    const auto *object = objectAt(mouse);
    if (!object || object->name.empty() || exitAt(mouse))
        return;
    const Vec p = screen(object->pos);
    // OpenDiablo2 HUD uses Font16; uncolored Object.Label text defaults to white.
    const int width = painter_.measure(object->name, 16);
    DrawRectangle(int(p.x - width / 2 - 8), int(p.y - 70), width + 16, 24, {0, 0, 0, 210});
    painter_.label(object->name, int(p.x - width / 2), int(p.y - 65), 16, WHITE);
}
} // namespace d2x
