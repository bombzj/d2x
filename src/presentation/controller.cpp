#include "controller.hpp"
#include "character_panel.hpp"
#include "skill_tree.hpp"
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
                session_.submit(UseClassSkill{*ui.leftSkill, enemy.pos, enemy.id});
            else
                session_.submit(Attack{enemy.id});
            return;
        }
    }
    if (const auto *object = view_.objectAt(mouse)) {
        session_.submit(Interact{object->id});
        pickupClick_ = true;
        return;
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
    if (ui.npcMenu) {
        int action = input.escape ? 4 :
                     input.insideViewport && input.leftPressed ? view_.clickNpcMenu(input.mouse) : 0;
        if (input.insideViewport && input.leftPressed && !action) action = 4;
        if (action == 1) view_.startNpcTalk();
        else if (action == 2) view_.openNpcShop();
        else if (action == 3)
            session_.submit(IdentifyWithCain{ui.dialogueObject});
        else if (action == 5) view_.showNextNpcGossip();
        else if (action == 4) {
            session_.submit(EndNpcConversation{ui.dialogueObject});
            ui.npcMenu = false;
        }
        return true;
    }
    if (!ui.blocksWorld()) {
        if (input.debugGold) {
            unsigned capacity = unsigned(session_.state().player.level) * 10000;
            unsigned amount = std::min(1000u, capacity - session_.state().player.gold);
            if (amount) session_.submit(DebugGrantGold{amount});
            view_.notice(amount ? "Gold +" + std::to_string(amount) : "Gold wallet is full.");
        }
        if (input.debugExperience) {
            auto remaining = session_.maximumExperience() - session_.state().player.experience;
            auto amount = std::min<uint64_t>(1000, remaining);
            if (session_.state().player.dead)
                view_.notice("Experience requires a living player.", true);
            else {
                if (amount) session_.submit(DebugGrantExperience{amount});
                view_.notice(amount ? "Experience +" + std::to_string(amount) : "Maximum level reached.");
            }
        }
        if (input.debugAttributes) {
            session_.submit(DebugResetAttributes{});
            view_.notice("Allocated attribute points returned.");
        }
        if (input.debugCharacter) {
            if (session_.state().player.dead)
                view_.notice("Switch character while alive.", true);
            else {
                session_.submit(DebugSwitchCharacter{});
                view_.notice("Switching character; level, attributes and skills reset.");
            }
        }
        if (input.debugTalents) {
            session_.submit(DebugResetSkills{});
            view_.notice("Allocated skill points returned.");
        }
    }
    if (ui.shopOpen) {
        if (input.escape) {
            ui.shopOpen = false;
            ui.npcMenu = true;
            ui.inventory.open = false;
        } else {
            if (input.pageDelta) view_.scrollNpcShop(-input.pageDelta);
            if (input.insideViewport && input.leftPressed)
                if (auto slot = view_.clickNpcShop(input.mouse))
                    session_.submit(BuyVendorItem{ui.dialogueObject, *slot});
        }
        return true;
    }
    if (!ui.dialogue.empty()) {
        if (input.escape) {
            ui.dialogue.clear();
            ui.dialogueLines.clear();
            ui.npcMenu = true;
        } else {
            if (input.pageDelta)
                view_.scrollNpcDialogue(-input.pageDelta * 3);
            if (input.insideViewport && input.leftPressed)
                view_.closeNpcDialogue();
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
        ui.skillTreeOpen = false;
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
    if (input.character && !ui.blocksWorld()) {
        ui.characterOpen = !ui.characterOpen;
        return true;
    }
    if (input.skillTree && !ui.blocksWorld()) {
        if (!session_.content().skills.tree(session_.characterCode())) {
            view_.notice("This MPQ profile has no skill tree layout.", true);
            return true;
        }
        bool opening = !ui.skillTreeOpen;
        if (opening && ui.inventory.open) toggleInventory();
        ui.skillTreeOpen = opening;
        ui.skillPicker.reset();
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
        ui.characterOpen = false;
        ui.skillTreeOpen = false;
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
        else if (ui.characterOpen)
            ui.characterOpen = false;
        else if (ui.skillTreeOpen)
            ui.skillTreeOpen = false;
        else
            return false;
        inventoryClick_ = input.leftHeld || input.leftReleased;
        inventoryRight_ = input.rightHeld;
        return true;
    }
    if (ui.travelMenu && !ui.help) {
        if (ui.waypointSource) {
            if (input.insideViewport && input.leftPressed)
                if (auto destination = view_.clickWaypointMenu(input.mouse)) {
                    session_.submit(WaypointTravel{ui.waypointSource, *destination});
                    ui.travelMenu = false;
                    ui.pause = false;
                }
            return true;
        }
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
    if (ui.characterOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), characterBounds())) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), characterClose()))
                ui.characterOpen = false;
            else if (session_.state().player.unspentAttributes > 0)
                if (auto attribute = characterAttributeAt(input.mouse))
                    session_.submit(AllocateAttribute{*attribute});
        }
        return true;
    }
    if (ui.skillTreeOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), skillTreeBounds())) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), skillTreeClose()))
                ui.skillTreeOpen = false;
            else {
                bool switched = false;
                for (int page = 1; page <= 3; ++page)
                    if (CheckCollisionPointRec(rv(input.mouse), skillTreeTab(page))) {
                        ui.skillPage = page;
                        switched = true;
                        break;
                    }
                if (!switched)
                    if (auto skill = view_.skillAt(input.mouse))
                        session_.submit(AllocateSkill{*skill});
            }
        }
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
                session_.submit(UseClassSkill{*ui.leftSkill, view_.world(input.mouse), {}});
            else
                click(input.mouse);
            repeatClick_ = 1.f / 6.f;
        }
        if (input.rightHeld && !inventoryRight_) {
            EntityId target;
            for (const auto &enemy : session_.state().area.enemies)
                if (enemy.hp > 0 && session_.active(enemy.pos) &&
                    (view_.screen(enemy.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                    target = enemy.id;
                    break;
                }
            if (ui.rightSkill)
                session_.submit(UseClassSkill{*ui.rightSkill, view_.world(input.mouse), target});
            else if (target)
                session_.submit(Attack{target});
        }
    }
    return true;
}
} // namespace d2x
