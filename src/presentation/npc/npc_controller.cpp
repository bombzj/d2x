#include "client/npc_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
bool SceneController::handleNpcMenu(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.npcMenu) {
        NpcMenuSelection selection;
        if (input.escape) selection.action = NpcMenuAction::Cancel;
        else if (input.insideViewport && input.leftPressed) selection = view_.clickNpcMenu(input.mouse);
        if (input.insideViewport && input.leftPressed && selection.action == NpcMenuAction::None)
            selection.action = NpcMenuAction::Cancel;
        const auto action = selection.action;
        if (action == NpcMenuAction::Talk) {
            view_.startNpcTalk();
        }
        else if (action == NpcMenuAction::Back) ui.npcTopics = false;
        else if (action == NpcMenuAction::TextTopic && selection.textTopic) view_.startNpcTextTopic(*selection.textTopic);
        else if (action == NpcMenuAction::Introduction) view_.startNpcIntroduction();
        else if (action == NpcMenuAction::Gossip) view_.showNextNpcGossip();
        else if (action == NpcMenuAction::QuestTopic && selection.quest) view_.startNpcTopic(*selection.quest);
        else if (action == NpcMenuAction::Trade) view_.openNpcShop();
        else if (action == NpcMenuAction::Gamble) {
            npcClient_.submit(OpenGamble{ui.dialogueObject});
        }
        else if (action == NpcMenuAction::Hire) npcClient_.submit(OpenHirelingList{ui.dialogueObject});
        else if (action == NpcMenuAction::Resurrect) npcClient_.submit(ResurrectHireling{ui.dialogueObject});
        else if (action == NpcMenuAction::Identify)
            npcClient_.submit(IdentifyWithCain{ui.dialogueObject});
        else if (action == NpcMenuAction::Respec)
            npcClient_.submit(ClaimAkaraRespec{ui.dialogueObject});
        else if (action == NpcMenuAction::Imbue || action == NpcMenuAction::Socket || action == NpcMenuAction::Personalize) {
            ui.inventoryQuestNpc = ui.dialogueObject;
            ui.inventoryNpcAction = action;
            ui.npcMenu = false;
            ui.inventory.open = true;
            ui.inventory.cancelGesture();
            const auto &hints = npcClient_.read(ui.dialogueObject).serviceHints;
            if (const auto hint = hints.find(action); hint != hints.end()) view_.notice(hint->second);
            else if (action == NpcMenuAction::Imbue) view_.notice("Select a plain weapon or armor to imbue.");
        }
        else if (action == NpcMenuAction::GoEast) {
            npcClient_.submit(CompleteActOne{ui.dialogueObject});
            ui.npcMenu = false;
        }
        else if (action == NpcMenuAction::Sail) {
            npcClient_.submit(CompleteActTwo{ui.dialogueObject});
            ui.npcMenu = false;
        }
        else if (action == NpcMenuAction::Cancel) {
            npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            ui.npcMenu = false;
        }
        return true;
    }
    return false;
}
bool SceneController::handleNpcDialogue(const FrameInput &input) {
    auto &ui = view_.ui();
    if (!ui.dialogue.empty()) {
        if (input.escape) {
            if (!view_.closeNpcDialogue()) npcClient_.submit(EndNpcConversation{ui.dialogueObject});
        } else {
            if (input.pageDelta)
                view_.scrollNpcDialogue(-input.pageDelta * 3);
            if (input.insideViewport && input.leftPressed) {
                if (!view_.closeNpcDialogue()) npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            }
        }
        return true;
    }
    return false;
}
} // namespace d2x
