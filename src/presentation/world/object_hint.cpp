#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "world/object.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
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
