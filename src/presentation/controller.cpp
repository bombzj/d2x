#include "controller.hpp"
#include "character_panel.hpp"
#include "skill_tree.hpp"
#include "quest_panel.hpp"
#include "hireling_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
void SceneController::openGameMenu(Vec mouse) {
    auto &ui = view_.ui();
    if (ui.inventory.open) toggleInventory();
    ui.inventory.cancelGesture();
    ui.skillPicker.reset();
    ui.characterOpen = ui.hirelingOpen = ui.skillTreeOpen = ui.questOpen = false;
    ui.pointButtonPressed.reset();
    ui.questPressed = -1;
    ui.gameMenuOpen = true;
    ui.gameMenuSelected = 2;
    ui.gameMenuPressed = -1;
    ui.gameMenuTime = 0;
    ui.gameMenuMouse = mouse;
    ui.showLoot = false;
    movement_ = {};
    leftCombatTarget_ = rightCombatTarget_ = {};
    channelInputSkill_ = -1;
    session_.submit(StopMoving{});
    session_.submit(StopChannel{});
}
void SceneController::click(Vec mouse) {
    auto &ui = view_.ui();
    view_.cancelNpcDialogue();
    if (auto portal = session_.cainPortalPosition(); portal &&
        (view_.screen(*portal) - Vec{0, 40} - mouse).length() < 45) {
        session_.submit(UseCainPortal{});
        pickupClick_ = true;
        return;
    }
    for (const auto &portal : session_.portals(session_.region().definition.id)) {
        if ((view_.screen(portal.position) - Vec{0, 40} - mouse).length() >= 45) continue;
        session_.submit(UseTownPortal{portal.revision});
        pickupClick_ = true;
        return;
    }
    if (const auto *exit = view_.exitAt(mouse)) {
        session_.submit(UseExit{exit->slot});
        pickupClick_ = true;
        return;
    }
    if (auto item = view_.lootAt(mouse, true)) {
        session_.submit(PickupItem{*item, ui.inventory.open});
        pickupClick_ = true;
        return;
    }
    for (const auto &enemy : session_.state().area.enemies) {
        if (enemy.hp > 0 && session_.active(enemy.pos) &&
            (view_.screen(enemy.pos) - Vec{0, 25} - mouse).length() < 24) {
            leftCombatTarget_ = enemy.id;
            leftTargetSkill_ = ui.leftSkill;
            if (ui.leftSkill)
                session_.submit(UseSkill{*ui.leftSkill, enemy.pos, enemy.id});
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
        session_.submit(PickupItem{*item, ui.inventory.open});
        pickupClick_ = true;
        return;
    }
    ui.clickAt = view_.world(mouse);
    ui.clickAge = 0;
    session_.submit(MoveTo{ui.clickAt});
}
bool SceneController::handle(const FrameInput &input, float elapsed) {
    auto &ui = view_.ui();
    ui.inventory.syncCursor(session_);
    if (!session_.state().player.hireling.active()) ui.hirelingOpen = false;
    if (!input.focused || ui.blocksWorld() || session_.state().player.dead ||
        input.escape || input.inventory || input.character || input.skillTree || input.quests ||
        input.hireling || input.storage || input.rightPressed || input.movement.length() > .1f) {
        ui.pointButtonPressed.reset();
        ui.questPressed = -1;
    }
    if (!ui.questOpen) ui.questPressed = -1;
    if (inputRegion_ != session_.state().area.region) {
        leftCombatTarget_ = rightCombatTarget_ = {};
        inputRegion_ = session_.state().area.region;
    }
    if (!input.focused || !input.leftHeld || input.leftPressed || input.rightPressed) leftCombatTarget_ = {};
    if (!input.focused || !input.rightHeld || input.rightPressed || input.leftPressed) rightCombatTarget_ = {};
    if (channelInputSkill_ >= 0 && (!input.focused || !input.rightHeld ||
        ((!input.insideViewport || hudSurface(input.mouse) ||
          !CheckCollisionPointRec(rv(input.mouse), view_.worldViewport())) && !rightCombatTarget_) ||
        ui.blocksWorld() || ui.inventory.drag || ui.inventory.split || ui.inventory.goldDialog ||
        ui.inventory.identify || input.movement.length() > .1f || input.leftPressed || input.leftHeld ||
        (input.escape && !ui.inventory.open) ||
        ui.rightSkill != channelInputSkill_)) {
        session_.submit(StopChannel{});
        channelInputSkill_ = -1;
    }
    temporaryRun_ = input.focused && input.control && !input.showLoot;
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
        ui.gameMenuPressed = -1;
        return true;
    }
    if (ui.gameMenuOpen) {
        leftCombatTarget_ = rightCombatTarget_ = {};
        ui.showLoot = false;
        auto resume = [&] {
            ui.gameMenuOpen = false;
            ui.gameMenuPressed = -1;
            releaseAfterLoad_ = true;
        };
        if (input.escape) {
            resume();
            return true;
        }
        const int hovered = input.insideViewport ? view_.gameMenuAt(input.mouse) : -1;
        if ((input.mouse - ui.gameMenuMouse).length() > 0 || input.leftPressed) {
            if (hovered >= 0) ui.gameMenuSelected = hovered;
            ui.gameMenuMouse = input.mouse;
        }
        if (input.menuDelta) {
            ui.gameMenuSelected = std::clamp(ui.gameMenuSelected + input.menuDelta, 0, 2);
            ui.gameMenuPressed = -1;
        }
        int activated = input.enter ? ui.gameMenuSelected : -1;
        if (input.leftPressed) ui.gameMenuPressed = hovered;
        if (input.leftReleased) {
            if (hovered >= 0 && hovered == ui.gameMenuPressed) activated = hovered;
            ui.gameMenuPressed = -1;
        } else if (!input.leftHeld) ui.gameMenuPressed = -1;
        if (activated == 1) return false;
        if (activated == 2) resume();
        return true;
    }
    if (ui.pointButtonPressed) {
        const bool skill = *ui.pointButtonPressed;
        const bool enabled = skill ? session_.state().player.unspentSkills > 0
                                   : session_.state().player.unspentAttributes > 0;
        if (input.leftReleased) {
            ui.pointButtonPressed.reset();
            if (enabled && input.insideViewport &&
                CheckCollisionPointRec(rv(input.mouse), skill ? hudSkillTreeButton() : hudCharacterButton())) {
                if (skill) { ui.skillTreeOpen = true; ui.skillPicker.reset(); }
                else ui.characterOpen = true;
            }
            return true;
        }
        if (!enabled || !input.leftHeld) ui.pointButtonPressed.reset();
        else return true;
    }
    if (ui.questPressed >= 0) {
        const int index = ui.questPressed;
        if (input.leftReleased) {
            ui.questPressed = -1;
            if (input.insideViewport && session_.quest(questDisplayOrder[size_t(index)]).stage &&
                CheckCollisionPointRec(rv(input.mouse), questIconBounds(index)))
                ui.questSelected = index;
            return true;
        }
        if (!input.leftHeld) ui.questPressed = -1;
        else return true;
    }
    repeatClick_ -= elapsed;
    if (ui.blocksWorld() || (input.escape && !ui.inventory.open) || input.weaponSwap ||
        input.movement.length() > .1f || session_.state().player.dead) {
        if (leftCombatTarget_ || rightCombatTarget_) {
            session_.submit(StopMoving{});
            session_.submit(StopChannel{});
        }
        leftCombatTarget_ = rightCombatTarget_ = {};
    }
    if (input.run)
        session_.submit(ToggleRun{});
    if (input.weaponSwap && session_.content().stashLayout.expansion &&
        !ui.npcMenu && ui.dialogue.empty() && !ui.pause && !ui.travelMenu &&
        !ui.inventory.drag && !ui.inventory.split && !ui.inventory.goldDialog &&
        !ui.shopConfirm && !ui.hireListOpen) {
        session_.submit(SwitchWeaponSet{});
        return true;
    }
    if (ui.npcMenu) {
        int action = input.escape ? 4 :
                     input.insideViewport && input.leftPressed ? view_.clickNpcMenu(input.mouse) : 0;
        if (input.insideViewport && input.leftPressed && !action) action = 4;
        if (action == 1) {
            view_.startNpcTalk();
        }
        else if (action == 11) view_.startNpcIntroduction();
        else if (action == 5) view_.showNextNpcGossip();
        else if (action >= 100 && action < 106) view_.startNpcTopic(ActOneQuest(action - 100));
        else if (action == 2) view_.openNpcShop();
        else if (action == 9) {
            session_.submit(OpenGamble{ui.dialogueObject});
        }
        else if (action == 10) session_.submit(OpenHirelingList{ui.dialogueObject});
        else if (action == 12) session_.submit(ResurrectHireling{ui.dialogueObject});
        else if (action == 3)
            session_.submit(IdentifyWithCain{ui.dialogueObject});
        else if (action == 6)
            session_.submit(ClaimAkaraRespec{ui.dialogueObject});
        else if (action == 7) {
            ui.imbueNpc = ui.dialogueObject;
            ui.npcMenu = false;
            ui.inventory.open = true;
            ui.inventory.cancelGesture();
            view_.notice("Select a plain weapon or armor to imbue.");
        }
        else if (action == 8) {
            session_.submit(CompleteActOne{ui.dialogueObject});
            ui.npcMenu = false;
            view_.notice("The passage east is open. Act II travel is not yet available.");
        }
        else if (action == 4) {
            session_.submit(EndNpcConversation{ui.dialogueObject});
            ui.npcMenu = false;
        }
        return true;
    }
    if (ui.hireListOpen) {
        const auto *offers = session_.hirelingOffers(ui.dialogueObject);
        const auto cancel = hirelingListCancel();
        if (input.escape || (input.insideViewport && input.leftPressed &&
            CheckCollisionPointRec(rv(input.mouse), cancel))) {
            ui.hireListOpen = false;
            session_.submit(EndNpcConversation{ui.dialogueObject});
            return true;
        }
        const int maximum = offers ? std::max(0, int(offers->size()) - hirelingVisibleRows) : 0;
        ui.hireListScroll = std::clamp(ui.hireListScroll + input.pageDelta - input.quantityDelta, 0, maximum);
        if (input.insideViewport && input.leftPressed) {
            const auto bar = hirelingScrollBounds();
            if (CheckCollisionPointRec(rv(input.mouse), bar)) {
                const float local = (input.mouse.y - bar.y) / classicPanelScale;
                if (local < 10) --ui.hireListScroll;
                else if (local > 320) ++ui.hireListScroll;
                else ui.hireListScroll = int(std::clamp((local - 10) / 310.f, 0.f, 1.f) * maximum + .5f);
                ui.hireListScroll = std::clamp(ui.hireListScroll, 0, maximum);
            } else if (offers) for (int row = 0; row < hirelingVisibleRows; ++row) {
                const int index = ui.hireListScroll + row;
                if (index >= int(offers->size())) break;
                if (CheckCollisionPointRec(rv(input.mouse), hirelingListRow(row))) {
                    session_.submit(HireMercenary{ui.dialogueObject, offers->at(size_t(index)).slot});
                    break;
                }
            }
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
        if (input.debugCube) {
            session_.submit(DebugDropCube{});
            view_.notice("Dropping the original cube near your feet.");
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
        if (input.debugTalents) {
            session_.submit(DebugResetSkills{});
            view_.notice("Allocated skill points returned.");
        }
        if (input.debugWaypoints) {
            session_.submit(DebugUnlockWaypoints{});
            view_.notice("Available waypoints activated.");
        }
    }
    if (ui.shopOpen) {
        auto repairItemAt = [&]() -> const ItemInstance * {
            const auto &inventory = session_.inventory();
            EntityId id;
            if (auto cell = inventoryCell(input.mouse))
                id = inventory.itemAt(session_.playerContainers().backpack, *cell);
            if (auto slot = equipmentAt(input.mouse, session_.state().player.weaponSet))
                id = inventory.equipped(session_.playerContainers(), *slot);
            return inventory.item(id);
        };
        if (ui.shopSalePending) return true;
        if (input.escape) {
            if ((ui.inventory.drag && !ui.inventory.drag->onCursor) || ui.inventory.identify || ui.inventory.split || ui.inventory.goldDialog)
                ui.inventory.cancelGesture();
            else if (ui.shopRepair) ui.shopRepair = false;
            else if (ui.shopConfirm)
                ui.shopConfirm.reset();
            else {
                view_.closeNpcShop();
                session_.submit(EndNpcConversation{ui.dialogueObject});
            }
        } else if (input.inventory || (!ui.shopConfirm && !ui.inventory.split && !ui.inventory.goldDialog &&
                   input.insideViewport && input.leftPressed &&
                   CheckCollisionPointRec(rv(input.mouse), inventoryClose()))) {
            view_.closeNpcShop();
            session_.submit(EndNpcConversation{ui.dialogueObject});
        } else if (ui.inventory.drag) {
            const auto *source = session_.inventory().item(ui.inventory.drag->item.id);
            if (!source || source->revision != ui.inventory.drag->item.revision || input.rightPressed)
                return handleInventory(input);
            if ((input.mouse - ui.inventory.drag->pressedAt).length() > 4)
                ui.inventory.drag->moved = true;
            const bool drop = ui.inventory.drag->pickedUp ? input.leftPressed :
                input.leftReleased && ui.inventory.drag->moved;
            if (input.insideViewport && CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
                if (drop) {
                    ui.inventory.drag->pickedUp = ui.inventory.drag->moved = true;
                    inventoryClick_ = true;
                    if (view_.npcShopDropAt(input.mouse)) {
                        if (session_.vendorSaleQuote(ui.dialogueObject, source->handle())) {
                            ui.shopSalePending = source->handle();
                            session_.submit(SellVendorItem{ui.dialogueObject, source->handle()});
                        } else view_.notice("That item cannot be sold here.", true);
                    }
                }
                return true;
            }
            return handleInventory(input);
        } else if (ui.inventory.identify || ui.inventory.split || ui.inventory.goldDialog) {
            return handleInventory(input);
        } else {
            if (input.pageDelta) view_.scrollNpcShop(-input.pageDelta);
            if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                       session_.content().stashLayout.expansion &&
                       weaponTabAt(input.mouse).has_value()) {
                if (*weaponTabAt(input.mouse) != session_.state().player.weaponSet)
                    session_.submit(SwitchWeaponSet{});
            } else if (input.enter && ui.shopConfirm) {
                const auto slot = *ui.shopConfirm;
                ui.shopConfirm.reset();
                session_.submit(BuyVendorItem{ui.dialogueObject, slot, ui.shopGamble});
            } else if (!ui.shopConfirm && input.insideViewport &&
                       (inventorySurface(ui.inventory, input.mouse) ||
                        CheckCollisionPointRec(rv(input.mouse), beltBounds(
                            session_.inventory().container(session_.playerContainers().belt)->spec.rows)))) {
                if (ui.shopRepair && input.leftPressed) {
                    if (const auto *item = repairItemAt())
                        session_.submit(RepairVendorItem{ui.dialogueObject, item->handle()});
                } else return handleInventory(input);
            } else if (input.insideViewport && input.leftPressed) {
                if (auto slot = view_.clickNpcShop(input.mouse))
                    session_.submit(BuyVendorItem{ui.dialogueObject, *slot, ui.shopGamble});
                if (!ui.shopOpen)
                    session_.submit(EndNpcConversation{ui.dialogueObject});
            } else if (input.insideViewport && input.rightPressed) {
                inventoryRight_ = true;
                if (auto slot = view_.clickNpcShop(input.mouse, true))
                    session_.submit(BuyVendorItem{ui.dialogueObject, *slot, ui.shopGamble});
            }
        }
        return true;
    }
    if (!ui.dialogue.empty()) {
        if (input.escape) {
            if (!view_.closeNpcDialogue()) session_.submit(EndNpcConversation{ui.dialogueObject});
        } else {
            if (input.pageDelta)
                view_.scrollNpcDialogue(-input.pageDelta * 3);
            if (input.insideViewport && input.leftPressed) {
                if (!view_.closeNpcDialogue()) session_.submit(EndNpcConversation{ui.dialogueObject});
            }
        }
        return true;
    }
    if (input.help) {
        ui.skillPicker.reset();
        ui.help = !ui.help;
    }
    if (input.automap) {
        if (input.shift) {
            ui.automapLarge = !ui.automapLarge;
            ui.automap = true;
        } else {
            ui.automap = !ui.automap;
        }
        ui.automapOffset = {};
    }
    if (input.minimapSide)
        ui.minimapRight = !ui.minimapRight;
    if (input.automapCenter)
        ui.automapOffset = {};
    if (input.automapNames)
        ui.automapNames = !ui.automapNames;
    if (input.travel) {
        ui.waypointSource = {};
        ui.travelPage = 0;
        ui.skillPicker.reset();
        session_.submit(CloseStorage{});
        ui.inventory.storage = {};
        ui.inventory.cubeOpen = false;
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
    }
    if (input.character && !ui.blocksWorld()) {
        ui.characterOpen = !ui.characterOpen;
        if (ui.characterOpen) ui.questOpen = ui.hirelingOpen = false;
        return true;
    }
    if (input.hireling && !ui.blocksWorld()) {
        ui.hirelingOpen = !ui.hirelingOpen && session_.state().player.hireling.active();
        if (ui.hirelingOpen) {
            if (ui.inventory.storage || ui.inventory.cubeOpen) toggleInventory();
            ui.characterOpen = ui.questOpen = false;
            ui.inventory.cancelGesture();
        }
        return true;
    }
    if ((input.quests || (ui.questNotice && !ui.questOpen && !ui.characterOpen &&
         !ui.inventory.storage && !ui.inventory.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), questNoticeBounds()))) && !ui.blocksWorld()) {
        ui.questOpen = !ui.questOpen;
        if (ui.questOpen) {
            ui.hirelingOpen = false;
            const bool updated = ui.questNotice;
            ui.questNotice = false;
            if (ui.questSelected < 0 || ui.questSelected >= int(questDisplayOrder.size()) ||
                !session_.quest(questDisplayOrder[size_t(ui.questSelected)]).stage) {
                ui.questSelected = -1;
                for (int index = 0; index < int(questDisplayOrder.size()); ++index)
                    if (session_.quest(questDisplayOrder[size_t(index)]).stage) {
                        ui.questSelected = index;
                        break;
                    }
            }
            if (updated && ui.questUpdated >= 0 && ui.questUpdated < int(questDisplayOrder.size()) &&
                session_.quest(questDisplayOrder[size_t(ui.questUpdated)]).stage)
                ui.questSelected = ui.questUpdated;
            ui.characterOpen = false;
            ui.skillTreeOpen = false;
            if (ui.inventory.open) toggleInventory();
        }
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
        if (opening) ui.questOpen = false;
        ui.skillPicker.reset();
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksWorld() &&
        session_.state().player.unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(input.mouse), hudCharacterButton())) {
        ui.pointButtonPressed = false;
        pickupClick_ = true;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksWorld() &&
        session_.state().player.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(input.mouse), hudSkillTreeButton())) {
        ui.pointButtonPressed = true;
        pickupClick_ = true;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksWorld() &&
        CheckCollisionPointRec(rv(input.mouse), hudMenuButton())) {
        inventoryClick_ = true;
        ui.miniPanelOpen = !ui.miniPanelOpen;
        return true;
    }
    if (ui.miniPanelOpen && !ui.blocksWorld() && input.insideViewport) {
        if (auto button = view_.miniPanelAt(input.mouse)) {
            if (input.leftPressed) {
                inventoryClick_ = true;
                if (*button == 4) {
                    view_.notice("This panel action is not available yet.", true);
                    return true;
                }
                if (*button == 6) {
                    openGameMenu(input.mouse);
                    return true;
                }
                FrameInput action;
                action.focused = true;
                if (*button == 0) action.character = true;
                if (*button == 1) action.inventory = true;
                if (*button == 2) action.skillTree = true;
                if (*button == 3) action.automap = true;
                if (*button == 5) action.quests = true;
                return handle(action, elapsed);
            }
            return true;
        }
    }
    if (input.collision)
        ui.debug = !ui.debug;
    if (input.pause)
        ui.pause = !ui.pause;
    if (input.mute)
        view_.toggleMute();
    if (input.restart) {
        ui.skillPicker.reset();
        ui.inventory.storage = {};
        ui.inventory.cubeOpen = false;
        ui.help = ui.pause = ui.travelMenu = false;
        ui.inventory.cancelGesture();
        ui.inventory.open = false;
        session_.submit(RestartArea{});
        ui.characterOpen = false;
        ui.skillTreeOpen = false;
        ui.questOpen = false;
    }
    if (input.escape) {
        if (ui.skillPicker) {
            ui.skillPicker.reset();
            skillGesture_ = true;
        } else if (ui.help)
            ui.help = false;
        else if (ui.travelMenu)
            ui.travelMenu = false;
        else if ((ui.inventory.drag && !ui.inventory.drag->onCursor) || ui.inventory.split)
            ui.inventory.cancelGesture();
        else if (ui.inventory.open) {
            toggleInventory();
            ui.imbueNpc = {};
        }
        else if (ui.characterOpen)
            ui.characterOpen = false;
        else if (ui.hirelingOpen)
            ui.hirelingOpen = false;
        else if (ui.skillTreeOpen)
            ui.skillTreeOpen = false;
        else if (ui.questOpen)
            ui.questOpen = false;
        else openGameMenu(input.mouse);
        inventoryClick_ = inventoryClick_ || input.leftPressed;
        inventoryRight_ = inventoryRight_ || input.rightPressed;
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
    // Skill hotkeys and belt keys remain live while a non-modal side panel is open.
    if (handleSkills(input)) return true;
    if (!ui.inventory.drag && !ui.inventory.split && !ui.inventory.goldDialog && !ui.inventory.identify)
        for (int column = 0; column < 4; ++column)
            if (input.belt[column]) session_.submit(UseBeltColumn{column, input.shift});
    if (view_.hirelingPortraitVisible() && input.insideViewport &&
        (CheckCollisionPointRec(rv(input.mouse), hirelingPortraitBounds()) ||
         CheckCollisionPointRec(rv(input.mouse), hirelingLifeBounds()))) {
        leftCombatTarget_ = rightCombatTarget_ = {};
        if (ui.inventory.drag) {
            const auto drag = *ui.inventory.drag;
            const bool drop = drag.pickedUp ? input.leftPressed : input.leftReleased;
            if (drop && queueInventory(UseHirelingPotion{drag.item}, drag.item.id)) ui.inventory.drag.reset();
        } else if (input.leftPressed && !ui.inventory.identify && !ui.inventory.pending) {
            ui.hirelingOpen = true;
            ui.characterOpen = ui.questOpen = false;
            session_.submit(StopMoving{});
            session_.submit(StopChannel{});
            channelInputSkill_ = -1;
            pickupClick_ = true;
        }
        return true;
    }
    if (leftCombatTarget_ || rightCombatTarget_) {
        const bool right = bool(rightCombatTarget_);
        const auto target = right ? rightCombatTarget_ : leftCombatTarget_;
        const auto skill = right ? ui.rightSkill : ui.leftSkill;
        const auto &enemies = session_.state().area.enemies;
        const auto found = std::find_if(enemies.begin(), enemies.end(),
            [&](const Enemy &enemy) { return enemy.id == target && enemy.hp > 0; });
        if (found != enemies.end() && session_.active(found->pos) &&
            skill == (right ? rightTargetSkill_ : leftTargetSkill_)) {
            if (skill) session_.submit(UseSkill{*skill, found->pos, target});
            else session_.submit(Attack{target});
        } else {
            session_.submit(StopMoving{});
            session_.submit(StopChannel{});
            channelInputSkill_ = -1;
        }
        return true;
    }
    if (ui.hirelingOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed && CheckCollisionPointRec(rv(input.mouse), hirelingClose())) {
            ui.hirelingOpen = false;
            ui.inventory.cancelGesture();
            return true;
        }
        if (ui.inventory.drag) return handleInventory(input);
        if (!ui.inventory.pending && input.leftPressed) {
            constexpr EquipmentSlot order[] = {EquipmentSlot::Head, EquipmentSlot::Torso,
                                               EquipmentSlot::RightHand, EquipmentSlot::RightHand};
            for (size_t index = 0; index < 4; ++index) {
                if (!CheckCollisionPointRec(rv(input.mouse), view_.hirelingSlotBounds(index))) continue;
                auto slots = session_.playerContainers(); slots.equipment = slots.hirelingEquipment;
                const auto id = session_.inventory().equipped(slots, order[index]);
                const auto *item = session_.inventory().item(id);
                if (item) {
                    queueInventory(EquipHirelingItem{item->handle(), std::nullopt,
                        ContainerLocation{slots.cursor, {}}}, id);
                }
                break;
            }
        }
        return true;
    }
    if (ui.characterOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), characterClose()))
                ui.characterOpen = false;
            else if (session_.state().player.unspentAttributes > 0)
                if (auto attribute = characterAttributeAt(input.mouse))
                    session_.submit(AllocateAttribute{*attribute});
        }
        return true;
    }
    if (ui.questOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), questCloseBounds()))
                ui.questOpen = false;
            else
                for (int index = 0; index < 6; ++index)
                    if (session_.quest(questDisplayOrder[size_t(index)]).stage &&
                        CheckCollisionPointRec(rv(input.mouse), questIconBounds(index))) {
                        ui.questPressed = index;
                        pickupClick_ = true;
                        break;
                    }
        }
        return true;
    }
    if (ui.skillTreeOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(true))) {
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
    if (handleInventory(input))
        return true;
    if (ui.imbueNpc) return true;
    if (ui.automap) {
        ui.automapOffset = ui.automapOffset - input.movement * (120.f * elapsed);
        movement_ = {};
    } else {
        movement_ = unproject(input.movement).unit();
    }
    if (!input.insideViewport)
        return true;
    if (!hudSurface(input.mouse) && CheckCollisionPointRec(rv(input.mouse), view_.worldViewport())) {
        if (input.leftPressed || (input.leftHeld && !pickupClick_ && repeatClick_ <= 0)) {
            if (input.shift) {
                EntityId target;
                for (const auto &enemy : session_.state().area.enemies)
                    if (enemy.hp > 0 && session_.active(enemy.pos) &&
                        (view_.screen(enemy.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                        target = enemy.id;
                        break;
                    }
                if (ui.leftSkill) session_.submit(UseSkill{*ui.leftSkill, view_.world(input.mouse), target, true});
                else session_.submit(Attack{target, false, false, view_.world(input.mouse), true});
            }
            else if (input.leftPressed)
                click(input.mouse);
            else {
                ui.clickAt = view_.world(input.mouse);
                ui.clickAge = 0;
                session_.submit(MoveTo{ui.clickAt});
            }
            repeatClick_ = 1.f / 6.f;
        }
        const auto *rightSkill = ui.rightSkill ? session_.content().skills.find(*ui.rightSkill) : nullptr;
        const bool channeled = rightSkill && rightSkill->spell &&
            rightSkill->spell->effect == SkillBehavior::Inferno;
        if (input.rightHeld && !inventoryRight_ && (!channeled ||
            (input.movement.length() <= .1f && !input.leftPressed && !input.leftHeld))) {
            EntityId target;
            for (const auto &enemy : session_.state().area.enemies)
                if (input.rightPressed && enemy.hp > 0 && session_.active(enemy.pos) &&
                    (view_.screen(enemy.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                    target = enemy.id;
                    break;
                }
            rightCombatTarget_ = target;
            rightTargetSkill_ = ui.rightSkill;
            Vec aim = view_.world(input.mouse);
            if (target)
                for (const auto &enemy : session_.state().area.enemies)
                    if (enemy.id == target) { aim = enemy.pos; break; }
            if (ui.rightSkill) {
                session_.submit(UseSkill{*ui.rightSkill, aim, target});
                if (channeled) channelInputSkill_ = *ui.rightSkill;
            } else
                session_.submit(Attack{target, false, false, aim});
        }
    }
    return true;
}
} // namespace d2x
