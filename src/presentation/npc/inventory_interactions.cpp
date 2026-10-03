#include "client/npc_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "gameplay/session/session.hpp"
#include "world/region.hpp"

namespace d2x {
bool SceneController::inventoryQuestTargetValid(EntityId object) const {
    return session_.canInsertStaff(object);
}
bool SceneController::openInventoryQuestTarget(Vec mouse, const InventoryItemView &item) {
    auto &panel = view_.ui();
    for (const auto &object : session_.region().objects) {
        if (!session_.canInsertStaff(object.id) || !view_.visible(object) ||
            (view_.screen(object.pos) + object.drawOffset - mouse).length() >= 24) continue;
        panel.orificeObject = object.id;
        panel.inventory.open = true;
        panel.inventory.cubeOpen = false;
        panel.questOpen = panel.characterOpen = panel.skillTreeOpen = false;
        if (item.definition == view_.inventoryView().staffRecipeOutput) panel.orificeItem = item.handle();
        panel.inventory.drag.reset();
        inventoryClick_ = true;
        return true;
    }
    return false;
}
void SceneController::submitInventoryQuest(EntityId object, ItemHandle item) {
    session_.submit(SubmitQuestItem{object, item});
}
void SceneController::submitImbue(ItemHandle item) {
    npcClient_.submit(ImbueItem{view_.ui().imbueNpc, item});
}
void SceneController::endInventoryNpcConversation(EntityId npc) {
    npcClient_.submit(EndNpcConversation{npc});
}
} // namespace d2x
