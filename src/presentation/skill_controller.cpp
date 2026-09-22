#include "controller.hpp"
#include "hud_layout.hpp"

namespace d2x {
bool SceneController::handleSkills(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.inventory.drag || ui.inventory.split || ui.inventory.open || session_.state().player.dead) {
        ui.skillPicker.reset();
        return false;
    }
    if (skillGesture_) {
        if (input.leftHeld || input.rightHeld)
            return true;
        skillGesture_ = false;
    }
    // Classic hotkeys select a mouse-button skill; they do not cast on keypress.
    for (size_t i = 0; i < input.skills.size(); ++i) {
        if (!input.skills[i])
            continue;
        auto skill = ui.hotbar[i];
        if (ui.skillPicker && !*ui.skillPicker) {
            if (view_.leftSkillAllowed(skill))
                ui.leftSkill = skill;
        } else
            ui.rightSkill = skill;
    }
    if (!input.insideViewport)
        return ui.skillPicker.has_value();
    if (ui.skillPicker) {
        bool right = *ui.skillPicker;
        if (input.leftPressed) {
            const auto choices = view_.skillChoices(right);
            for (size_t i = 0; i < choices.size(); ++i)
                if (CheckCollisionPointRec(rv(input.mouse), hudPickerSlot(right, int(i)))) {
                    (right ? ui.rightSkill : ui.leftSkill) = choices[i];
                    break;
                }
        }
        if (input.leftPressed || input.rightPressed) {
            ui.skillPicker.reset();
            skillGesture_ = true;
        }
        return true;
    }
    if (input.leftPressed)
        for (bool right : {false, true})
            if (CheckCollisionPointRec(rv(input.mouse), hudSkillSlot(right))) {
                ui.skillPicker = right;
                ui.inventory.beltExpanded = false;
                skillGesture_ = true;
                session_.submit(StopMoving{});
                return true;
            }
    if (input.leftPressed && CheckCollisionPointRec(rv(input.mouse), hudRunButton())) {
        session_.submit(ToggleRun{});
        return true;
    }
    return false;
}
} // namespace d2x
