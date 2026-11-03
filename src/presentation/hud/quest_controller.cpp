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
            if (input.insideViewport && view_.questView().entry(view_.questView().displayed(ui.questAct, size_t(index))).selectable() &&
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
    if ((input.quests || (ui.questNotice && !ui.questOpen && !ui.partyOpen && !ui.characterOpen &&
         !ui.inventory.storage && !ui.inventory.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), questNoticeBounds()))) && !ui.capturesWorldInput()) {
        ui.questAutoPreview = false;
        ui.questOpen = !ui.questOpen;
        if (ui.questOpen) {
            view_.openQuestLog();
            ui.partyOpen = false;
            ui.hirelingOpen = false;
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
        if(input.leftPressed || input.rightPressed || input.mouseWheel) ui.questAutoPreview=false;
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
                for (int index = 0; index < int(view_.questView().quests(ui.questAct).size()); ++index)
                    if (view_.questView().entry(view_.questView().displayed(ui.questAct, size_t(index))).selectable() &&
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
