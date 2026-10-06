#include "presentation/scene_view.hpp"

namespace d2x {
const ExitView *SceneView::exitAt(Vec mouse) const {
    if (hudSurface(mouse) || view_.capturesWorldInput() || !CheckCollisionPointRec(rv(mouse), worldViewport()))
        return nullptr;
    for (const auto &exit : mapView().exits) {
        auto p = screen(exit.position);
        const auto &r = exit.selection;
        Rectangle bounds{p.x + r.x * view_.zoom, p.y + r.y * view_.zoom,
                         r.width * view_.zoom, r.height * view_.zoom};
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
} // namespace d2x
