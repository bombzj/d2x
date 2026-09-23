#include "controller.hpp"
#include <algorithm>

namespace d2x {
void SceneController::click(Vec mouse) {
    auto &ui = view_.ui();
    ui.dialogue.clear();
    if (auto portal = session_.portalPosition(); portal &&
        (view_.screen(*portal) - Vec{0, 40} - mouse).length() < 45) {
        session_.submit(UseTownPortal{session_.state().portal.revision});
        pickupClick_ = true;
        return;
    }
    if (const auto *exit = view_.exitAt(mouse)) {
        session_.submit(UseExit{exit->slot});
        pickupClick_ = true;
        return;
    }
    if (auto item = view_.lootAt(mouse, true)) {
        session_.submit(PickupItem{*item});
        pickupClick_ = true;
        return;
    }
    for (const auto &enemy : session_.state().area.enemies) {
        if (enemy.hp > 0 && session_.active(enemy.pos) &&
            (view_.screen(enemy.pos) - Vec{0, 25} - mouse).length() < 24) {
            if (ui.leftSkill)
                session_.submit(CastSkill{*ui.leftSkill, enemy.pos});
            else
                session_.submit(Attack{enemy.id});
            return;
        }
    }
    for (const auto &object : session_.region().objects) {
        if (view_.visible(object) && object.interaction != Interaction::None &&
            (view_.screen(object.pos) - Vec{0, 25} - mouse).length() < 28) {
            session_.submit(Interact{object.id});
            pickupClick_ = true;
            return;
        }
    }
    if (auto item = view_.lootAt(mouse)) {
        session_.submit(PickupItem{*item});
        pickupClick_ = true;
        return;
    }
    ui.clickAt = view_.world(mouse);
    ui.clickAge = 0;
    session_.submit(MoveTo{ui.clickAt});
}
bool SceneController::handle(const FrameInput &input, float elapsed) {
    auto &ui = view_.ui();
    ui.showLoot = input.showLoot;
    if (releaseAfterLoad_) {
        movement_ = {};
        if (input.leftHeld || input.rightHeld || input.movement.length() > .1f)
            return true;
        releaseAfterLoad_ = false;
    }
    if (!input.leftHeld && !input.leftReleased)
        inventoryClick_ = false;
    if (!input.rightHeld)
        inventoryRight_ = false;
    if (!input.leftHeld)
        pickupClick_ = false;
    movement_ = {};
    if (!input.focused) {
        ui.inventory.cancelGesture();
        ui.skillPicker.reset();
        return true;
    }
    repeatClick_ -= elapsed;
    if (!ui.dialogue.empty()) {
        if (input.escape) {
            session_.submit(EndNpcConversation{ui.dialogueObject});
            ui.dialogue.clear();
            ui.dialogueLines.clear();
        } else {
            if (input.pageDelta)
                view_.scrollNpcDialogue(-input.pageDelta * 3);
            if (input.insideViewport && input.leftPressed) {
                if (view_.clickNpcDialogue(input.mouse))
                    session_.submit(IdentifyWithCain{ui.dialogueObject});
                else if (ui.dialogue.empty())
                    session_.submit(EndNpcConversation{ui.dialogueObject});
            }
        }
        return true;
    }
    if (input.help) {
        ui.skillPicker.reset();
        ui.help = !ui.help;
    }
    if (input.automap)
        ui.automap = !ui.automap;
    if (input.travel) {
        ui.waypointSource = {};
        ui.travelPage = 0;
        ui.skillPicker.reset();
        session_.submit(CloseStorage{});
        ui.inventory.storage = {};
        ui.travelMenu = !ui.travelMenu;
        ui.inventory.cancelGesture();
        ui.inventory.open = false;
    }
    if (input.storage && !ui.blocksWorld()) {
        if (ui.inventory.storage)
            toggleInventory();
        else {
            bool found = false;
            for (const auto &object : session_.region().objects) {
                if (object.interaction != Interaction::Stash)
                    continue;
                session_.submit(Interact{object.id});
                view_.notice("Walking to your private stash.", false);
                found = true;
                break;
            }
            if (!found)
                view_.notice("No private stash in this area. Return to camp.", true);
        }
        return true;
    }
    if (input.inventory) {
        toggleInventory();
        inventoryClick_ = input.leftHeld || input.leftReleased;
        inventoryRight_ = input.rightHeld;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksWorld() &&
        CheckCollisionPointRec(rv(input.mouse), inventoryToggle())) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (input.collision)
        ui.debug = !ui.debug;
    if (input.pause)
        ui.pause = !ui.pause;
    if (input.mute)
        view_.toggleMute();
    if (input.run)
        session_.submit(ToggleRun{});
    if (input.restart) {
        ui.skillPicker.reset();
        ui.inventory.storage = {};
        ui.help = ui.pause = ui.travelMenu = false;
        ui.inventory.cancelGesture();
        ui.inventory.open = false;
        session_.submit(RestartArea{});
    }
    if (input.escape) {
        if (ui.skillPicker) {
            ui.skillPicker.reset();
            skillGesture_ = true;
        } else if (ui.help)
            ui.help = false;
        else if (ui.travelMenu)
            ui.travelMenu = false;
        else if (ui.inventory.drag || ui.inventory.split)
            ui.inventory.cancelGesture();
        else if (ui.inventory.open)
            toggleInventory();
        else
            return false;
        inventoryClick_ = input.leftHeld || input.leftReleased;
        inventoryRight_ = input.rightHeld;
        return true;
    }
    if (ui.travelMenu && !ui.help) {
        const auto entries = view_.travelEntries();
        int pages = (int(entries.size()) + worldPageSize - 1) / worldPageSize;
        int delta = input.pageDelta;
        if (input.insideViewport && input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), travelPageButton(false)))
                --delta;
            if (CheckCollisionPointRec(rv(input.mouse), travelPageButton(true)))
                ++delta;
        }
        ui.travelPage = std::clamp(ui.travelPage + delta, 0, std::max(0, pages - 1));
        for (int i = 0; i < worldPageSize && ui.travelPage * worldPageSize + i < int(entries.size()); ++i) {
            if ((i < int(input.belt.size()) && input.belt[i]) ||
                (input.insideViewport && input.leftPressed &&
                 CheckCollisionPointRec(rv(input.mouse), travelSlot(i)))) {
                const auto &entry = entries[ui.travelPage * worldPageSize + i];
                if (!entry.destination) {
                    view_.notice(entry.status + (entry.missing.empty() ? "" : ": " + entry.missing.front()),
                                 true);
                    return true;
                }
                if (ui.waypointSource)
                    session_.submit(WaypointTravel{ui.waypointSource, *entry.destination});
                else
                    session_.submit(Travel{*entry.destination});
                ui.travelMenu = false;
                // Opening a paused travel panel does not trap its queued transition.
                ui.pause = false;
                break;
            }
        }
        return true;
    }
    if (ui.blocksWorld()) {
        ui.skillPicker.reset();
        ui.inventory.cancelGesture();
        return true;
    }
    if (input.expandBelt) {
        ui.skillPicker.reset();
        ui.inventory.beltExpanded = !ui.inventory.beltExpanded;
    }
    if (!ui.inventory.drag && !ui.inventory.split)
        for (int i = 0; i < 4; ++i)
            if (input.belt[i])
                session_.submit(UseBeltColumn{i});
    if (handleSkills(input) || handleInventory(input))
        return true;
    movement_ = unproject(input.movement).unit();
    if (!input.insideViewport)
        return true;
    if (!hudSurface(input.mouse)) {
        if (input.leftPressed || (input.leftHeld && !pickupClick_ && repeatClick_ <= 0)) {
            if (input.shift && ui.leftSkill)
                session_.submit(CastSkill{*ui.leftSkill, view_.world(input.mouse)});
            else
                click(input.mouse);
            repeatClick_ = 1.f / 6.f;
        }
        if (input.rightHeld && !inventoryRight_) {
            if (ui.rightSkill)
                session_.submit(CastSkill{*ui.rightSkill, view_.world(input.mouse)});
            else
                for (const auto &enemy : session_.state().area.enemies)
                    if (enemy.hp > 0 && session_.active(enemy.pos) &&
                        (view_.screen(enemy.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                        session_.submit(Attack{enemy.id});
                        break;
                    }
        }
    }
    return true;
}
} // namespace d2x
