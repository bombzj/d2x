#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "content/classic_data.hpp"
#include "world/object.hpp"
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
void SceneView::drawObjectHint(Vec mouse) const {
    if (view_.inventory.drag || view_.blocksWorld() || hudSurface(mouse) ||
        !CheckCollisionPointRec(rv(mouse), worldViewport()))
        return;
    if (const auto *corpse = playerCorpseAt(mouse)) {
        const auto &strings = localSession().content().itemStrings;
        const auto found = strings.find("corpse");
        const std::string label = localSession().state().player.character.name + " " +
            (found == strings.end() ? "Corpse" : found->second);
        const auto p = screen(corpse->position);
        drawCorpseLabel(label, p);
        return;
    }
    const auto *object = objectAt(mouse);
    if (!object || object->name.empty() || exitAt(mouse))
        return;
    const Vec p = objectScreen(*object);
    std::string label = object->name;
    if (object->chest && object->chest->locked) {
        const auto &strings = localSession().content().itemStrings;
        if (auto name = strings.find("lockedchest"); name != strings.end()) label = name->second;
    }
    // OpenDiablo2 HUD uses Font16; uncolored Object.Label text defaults to white.
    drawInteractionLabel(label, p);
}
} // namespace d2x
