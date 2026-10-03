#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/hud/quest_panel.hpp"

namespace d2x {
bool SceneController::handleQuestPress(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.questPressed >= 0) {
        const int index = ui.questPressed;
        if (input.leftReleased) {
            ui.questPressed = -1;
            if (input.insideViewport && view_.questView().entry(displayedQuest(ui.questAct, index)).active &&
                CheckCollisionPointRec(rv(input.mouse), questIconBounds(index)))
                ui.questSelected = index;
            return true;
        }
        if (!input.leftHeld) ui.questPressed = -1;
        else return true;
    }
    return false;
}
bool SceneController::handleQuestToggle(const FrameInput &input) {
    auto &ui = view_.ui();
    if ((input.quests || (ui.questNotice && !ui.questOpen && !ui.characterOpen &&
         !ui.inventory.storage && !ui.inventory.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), questNoticeBounds()))) && !ui.blocksWorld()) {
        ui.questOpen = !ui.questOpen;
        if (ui.questOpen) {
            ui.hirelingOpen = false;
            const bool updated = ui.questNotice;
            if (updated && ui.questUpdated >= 0) ui.questAct = ui.questUpdated / 6;
            else ui.questAct = view_.questView().currentAct;
            ui.questNotice = false;
            if (ui.questSelected < 0 || ui.questSelected >= int(questDisplayOrder.size()) ||
                !view_.questView().entry(displayedQuest(ui.questAct, ui.questSelected)).active) {
                ui.questSelected = -1;
                for (int index = 0; index < int(questDisplayOrder.size()); ++index)
                    if (view_.questView().entry(displayedQuest(ui.questAct, index)).active) {
                        ui.questSelected = index;
                        break;
                    }
            }
            if (updated && ui.questUpdated >= 0 && ui.questUpdated < int(QuestId::Count) &&
                view_.questView().entry(QuestId(ui.questUpdated)).active)
                ui.questSelected = ui.questUpdated % 6;
            ui.characterOpen = false;
            ui.skillTreeOpen = false;
            if (ui.inventory.open) toggleInventory();
        }
        return true;
    }
    return false;
}
bool SceneController::handleQuestPanel(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.questOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), questCloseBounds()))
                ui.questOpen = false;
            else {
                const int count = view_.questView().tabCount;
                for (int act = 0; act < count; ++act)
                    if (CheckCollisionPointRec(rv(input.mouse), questTabBounds(act))) {
                        ui.questAct = act;
                        ui.questSelected = -1;
                        ui.questPressed = -1;
                        return true;
                    }
                for (int index = 0; index < 6; ++index)
                    if (view_.questView().entry(displayedQuest(ui.questAct, index)).active &&
                        CheckCollisionPointRec(rv(input.mouse), questIconBounds(index))) {
                        ui.questPressed = index;
                        pickupClick_ = true;
                        break;
                    }
            }
        }
        return true;
    }
    return false;
}
} // namespace d2x
