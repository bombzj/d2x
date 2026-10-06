#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawCorpseLabel(const std::string &label, Vec p) const {
    drawInteractionLabel(label, p, 40);
}
void SceneView::drawInteractionLabel(const std::string &label, Vec p, int offset) const {
    const int width = painter_.measure(label, 16);
    DrawRectangle(int(p.x - width / 2 - 8), int(p.y - offset), width + 16, 24, {0, 0, 0, 210});
    painter_.label(label, int(p.x - width / 2), int(p.y - offset + 5), 16, WHITE);
}
} // namespace d2x
