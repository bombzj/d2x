#include "client/actor_client.hpp"
#include "client/character_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "hud_layout.hpp"

namespace d2x {
bool SceneController::handleSkills(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.inventory.drag || ui.inventory.split || ui.inventory.goldDialog || ui.inventory.identify ||
        view_.characterView().dead) {
        ui.skillPicker.reset();
        return false;
    }
    if (skillGesture_) {
        if (input.leftHeld || input.rightHeld)
            return true;
        skillGesture_ = false;
    }
    // In the open picker, F1-F8 bind the hovered icon. Otherwise they select
    // the saved skill for its original mouse button without casting it.
    const auto &hotkeys = view_.characterView().skillHotkeys;
    for (size_t i = 0; i < input.skills.size(); ++i) {
        if (!input.skills[i])
            continue;
        if (ui.skillPicker) {
            if (!input.insideViewport) continue;
            bool right = *ui.skillPicker;
            const auto choices = view_.skillChoices(right);
            for (size_t slot = 0; slot < choices.size(); ++slot)
                if (CheckCollisionPointRec(rv(input.mouse), hudPickerSlot(right, int(slot), int(choices.size())))) {
                    characterClient_.submit(BindSkillHotkey{unsigned(i), choices[slot].value_or(-1), right});
                    break;
                }
        } else if (hotkeys[i].skill != -2) {
            auto skill = hotkeys[i].skill;
            if (skill >= 0 && !(view_.characterView().skill(skill) && view_.characterView().skill(skill)->available)) continue;
            (hotkeys[i].right ? ui.rightSkill : ui.leftSkill) =
                skill < 0 ? std::nullopt : std::optional<int>{skill};
            characterClient_.submit(SelectMouseSkill{skill, hotkeys[i].right});
        }
    }
    if (!input.insideViewport)
        return ui.skillPicker.has_value();
    if (ui.skillPicker) {
        bool right = *ui.skillPicker;
        if (input.leftPressed) {
            const auto choices = view_.skillChoices(right);
            for (size_t i = 0; i < choices.size(); ++i)
                if (CheckCollisionPointRec(rv(input.mouse), hudPickerSlot(right, int(i), int(choices.size())))) {
                    (right ? ui.rightSkill : ui.leftSkill) = choices[i];
                    characterClient_.submit(SelectMouseSkill{choices[i].value_or(-1), right});
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
                ui.skillTreeOpen = false;
                ui.inventory.beltExpanded = false;
                skillGesture_ = true;
                actorClient_.stopMoving();
                return true;
            }
    if (input.leftPressed && CheckCollisionPointRec(rv(input.mouse), hudRunButton())) {
        actorClient_.toggleRun();
        return true;
    }
    return false;
}
} // namespace d2x
