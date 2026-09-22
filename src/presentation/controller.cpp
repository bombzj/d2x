#include "controller.hpp"
#include <algorithm>

namespace d2x {
void SceneController::click(Vec mouse) {
    auto &ui = view_.ui();
    ui.dialogue.clear();
    if (auto item = view_.lootAt(mouse, true)) {
        session_.submit(PickupItem{*item});
        pickupClick_ = true;
        return;
    }
    for (const auto &enemy : session_.state().area.enemies) {
        if (enemy.hp > 0 && (view_.screen(enemy.pos) - Vec{0, 25} - mouse).length() < 24) {
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
        return true;
    }
    repeatClick_ -= elapsed;
    if (input.help)
        ui.help = !ui.help;
    if (input.automap)
        ui.automap = !ui.automap;
    if (input.travel) {
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
        ui.inventory.storage = {};
        ui.help = ui.pause = ui.travelMenu = false;
        ui.inventory.cancelGesture();
        ui.inventory.open = false;
        session_.submit(RestartArea{});
    }
    if (input.escape) {
        if (ui.help)
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
        const auto &entries = session_.worldEntries();
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
        ui.inventory.cancelGesture();
        return true;
    }
    if (input.expandBelt)
        ui.inventory.beltExpanded = !ui.inventory.beltExpanded;
    if (!ui.inventory.drag && !ui.inventory.split)
        for (int i = 0; i < 4; ++i)
            if (input.belt[i])
                session_.submit(UseBeltColumn{i});
    if (handleInventory(input))
        return true;
    movement_ = unproject(input.movement).unit();
    if (!input.insideViewport)
        return true;
    if (input.mouse.y < H - HUD) {
        if (input.leftPressed || (input.leftHeld && !pickupClick_ && repeatClick_ <= 0)) {
            click(input.mouse);
            repeatClick_ = 1.f / 6.f;
        }
        if (input.rightHeld && !inventoryRight_)
            session_.submit(CastSkill{ui.hotbar.at(ui.selected), view_.world(input.mouse)});
        for (int i = 0; i < int(hotbarSlots); ++i)
            if (input.skills[i])
                session_.submit(CastSkill{ui.hotbar[i], view_.world(input.mouse)});
    } else if (input.leftPressed) {
        for (int i = 0; i < int(hotbarSlots); ++i)
            if (CheckCollisionPointRec(rv(input.mouse), skillSlot(i)))
                ui.selected = i;
    }
    return true;
}
} // namespace d2x
