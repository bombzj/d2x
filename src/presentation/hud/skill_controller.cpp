#include "client/actor_client.hpp"
#include "client/character_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "hud_layout.hpp"
#include <algorithm>

namespace d2x {
bool SceneController::handleSkills(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.inventory.drag || ui.inventory.goldDialog || ui.inventory.identify ||
        view_.characterView().dead) {
        ui.skillPicker.reset();
        return false;
    }
    if (skillGesture_) {
        if (input.leftHeld || input.rightHeld)
            return true;
        skillGesture_ = false;
    }
    if (input.skillPicker) {
        if (ui.skillPicker) ui.skillPicker.reset();
        else {
            ui.skillPicker = true; // Keyboard shortcut opens the right-hand skills.
            ui.skillTreeOpen = false;
            ui.inventory.beltExpanded = false;
            actorClient_.stopMoving();
        }
        cancelWorldGesture();
        releaseAfterLoad_ = input.leftHeld || input.rightHeld;
        return true;
    }
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
            const SelectMouseSkill selection{skill, hotkeys[i].right, hotkeys[i].owner};
            quickCast_.reset();
            lastSkillHotkey_ = i;
            if (ui.bhEnabled && ui.bhQuickCast && input.primarySkillPresses[i] &&
                !input.leftPressed && !input.rightPressed && !input.weaponSwap) {
                quickCast_ = QuickCastPending{selection, view_.characterView().weaponSet,
                    std::chrono::steady_clock::now() + std::chrono::seconds(1)};
                cancelWorldGesture();
                break;
            }
            (hotkeys[i].right ? ui.rightSkill : ui.leftSkill) =
                skill < 0 ? std::nullopt : std::optional<int>{skill};
            characterClient_.submit(selection);
            cancelWorldGesture();
            releaseAfterLoad_ = input.leftHeld || input.rightHeld;
        }
    }
    if (input.previousSkill || input.nextSkill) {
        quickCast_.reset();
        // Retail D2Client 3D6D0 cycles sixteen hotkey slots from the last
        // selected slot, skipping left-hand bindings and unavailable owners.
        const int direction = input.previousSkill ? -1 : 1;
        for (size_t count = 1; count <= hotkeys.size(); ++count) {
            const auto index = size_t((int(lastSkillHotkey_) + direction * int(count) + int(hotkeys.size())) % int(hotkeys.size()));
            const auto &hotkey = hotkeys[index];
            if (!hotkey.right || hotkey.skill == -2) continue;
            const auto *skill = hotkey.skill >= 0 ? view_.characterView().skill(hotkey.skill, hotkey.owner) : nullptr;
            if (hotkey.skill >= 0 && (!skill || !skill->available)) continue;
            ui.rightSkill = hotkey.skill < 0 ? std::nullopt : std::optional<int>{hotkey.skill};
            characterClient_.submit(SelectMouseSkill{hotkey.skill, true, hotkey.owner});
            lastSkillHotkey_ = index;
            cancelWorldGesture();
            releaseAfterLoad_ = input.leftHeld || input.rightHeld;
            break;
        }
        return true;
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
        toggleRunning();
        return true;
    }
    return false;
}
void SceneController::handleQuickCast(const FrameInput &input, const WorldInputView &world) {
    if (!world.combatRequestReady) return;
    const auto selection = quickCast_->selection;
    const auto skill = uint16_t(std::max(0, selection.skill));
    const auto hand = size_t(selection.right);
    const InputSkillSelection expected{skill, selection.owner == UINT32_MAX
        ? EntityId{} : EntityId{uint64_t(selection.owner) + 1}};
    if (world.skills[hand] != expected) {
        if (quickCast_->selectionRequested) return;
        if (characterClient_.submit(selection) && world.quickCastSkillSupported)
            quickCast_->selectionRequested = true;
        else
            quickCast_.reset();
        return;
    }
    quickCast_.reset();
    if (!world.quickCastSkillSupported) return;

    const auto &targets = world.validCombatTargets[hand];
    ActorControlIntent intent;
    intent.action = ActorControlIntent::Action::Cast;
    intent.right = selection.right;
    intent.point = world.point;
    if (world.combat && std::find(targets.begin(), targets.end(), world.combat) != targets.end())
        intent.target = world.combat;
    intent.stationary = input.standStill;
    intent.expectedSkill = ActorControlIntent::CastSkill{skill, selection.owner};
    actorClient_.control(intent);
}
} // namespace d2x
