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
            for (const auto &slot : view_.skillPickerSlots(right))
                if (CheckCollisionPointRec(rv(input.mouse), slot.bounds)) {
                    characterClient_.submit(BindSkillHotkey{unsigned(i), slot.skill.value_or(-1), right, slot.owner});
                    break;
                }
        } else if (hotkeys[i].skill != -2) {
            auto skill = hotkeys[i].skill;
            if (skill >= 0 && !(view_.characterView().skill(skill,hotkeys[i].owner) && view_.characterView().skill(skill,hotkeys[i].owner)->available)) continue;
            (hotkeys[i].right ? ui.rightSkill : ui.leftSkill) =
                skill < 0 ? std::nullopt : std::optional<int>{skill};
            characterClient_.submit(SelectMouseSkill{skill, hotkeys[i].right,hotkeys[i].owner});
            cancelWorldGesture();
            releaseAfterLoad_ = input.leftHeld || input.rightHeld;
        }
    }
    if (!input.insideViewport)
        return ui.skillPicker.has_value();
    if (ui.skillPicker) {
        bool right = *ui.skillPicker;
        if (input.leftPressed) {
            for (const auto &slot : view_.skillPickerSlots(right))
                if (CheckCollisionPointRec(rv(input.mouse), slot.bounds)) {
                    (right ? ui.rightSkill : ui.leftSkill) = slot.skill;
                    characterClient_.submit(SelectMouseSkill{slot.skill.value_or(-1), right, slot.owner});
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
