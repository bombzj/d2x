#include "client/npc_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
void SceneController::submitNpcItemService(ItemHandle item) {
    if (view_.ui().inventoryNpcAction == NpcMenuAction::Socket) npcClient_.submit(SocketQuestItem{view_.ui().inventoryQuestNpc, item});
    else if (view_.ui().inventoryNpcAction == NpcMenuAction::Personalize) npcClient_.submit(PersonalizeQuestItem{view_.ui().inventoryQuestNpc, item});
    else npcClient_.submit(ImbueItem{view_.ui().inventoryQuestNpc, item});
}
void SceneController::endInventoryNpcConversation(EntityId npc) {
    npcClient_.submit(EndNpcConversation{npc});
}
} // namespace d2x
