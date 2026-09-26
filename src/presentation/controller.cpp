#include "controller.hpp"
#include "character_panel.hpp"
#include "skill_tree.hpp"
#include "quest_panel.hpp"
#include "hireling_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
void SceneController::click(Vec mouse) {
    auto &ui = view_.ui();
    view_.cancelNpcDialogue();
    if (auto portal = session_.cainPortalPosition(); portal &&
        (view_.screen(*portal) - Vec{0, 40} - mouse).length() < 45) {
        session_.submit(UseCainPortal{});
        pickupClick_ = true;
        return;
    }
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
            leftCombatTarget_ = enemy.id;
            leftTargetSkill_ = ui.leftSkill;
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
    if (inputRegion_ != session_.state().area.region) {
        leftCombatTarget_ = rightCombatTarget_ = {};
        inputRegion_ = session_.state().area.region;
    }
    if (!input.focused || !input.leftHeld || input.leftPressed || input.rightPressed) leftCombatTarget_ = {};
    if (!input.focused || !input.rightHeld || input.rightPressed || input.leftPressed) rightCombatTarget_ = {};
    if (channelInputSkill_ >= 0 && (!input.focused || !input.rightHeld ||
        ((!input.insideViewport || hudSurface(input.mouse)) && !rightCombatTarget_) ||
        ui.blocksWorld() || ui.inventory.open ||
        input.movement.length() > .1f || input.leftPressed || input.leftHeld || input.inventory || input.escape ||
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
        return true;
    }
    repeatClick_ -= elapsed;
    if (ui.blocksWorld() || input.escape || input.inventory || input.weaponSwap ||
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
        auto saleItemAt = [&]() -> const ItemInstance * {
            const auto &inventory = session_.inventory();
            EntityId id;
            if (auto cell = inventoryCell(input.mouse))
                id = inventory.itemAt(session_.playerContainers().backpack, *cell);
            if (auto slot = equipmentAt(input.mouse, session_.state().player.weaponSet))
                id = inventory.equipped(session_.playerContainers(), *slot);
            return inventory.item(id);
        };
        if (ui.shopSaleConfirm) {
            if (input.escape) ui.shopSaleConfirm.reset();
            else if (input.enter) {
                auto item = *ui.shopSaleConfirm;
                ui.shopSaleConfirm.reset();
                session_.submit(SellVendorItem{ui.dialogueObject, item});
            } else if (input.insideViewport && input.leftPressed)
                if (auto item = view_.clickNpcSaleConfirm(input.mouse))
                    session_.submit(SellVendorItem{ui.dialogueObject, *item});
            return true;
        }
        if (input.escape) {
            if (ui.shopRepair) ui.shopRepair = false;
            else if (ui.shopConfirm)
                ui.shopConfirm.reset();
            else {
                view_.closeNpcShop();
                session_.submit(EndNpcConversation{ui.dialogueObject});
            }
        } else {
            if (input.pageDelta) view_.scrollNpcShop(-input.pageDelta);
            if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                CheckCollisionPointRec(rv(input.mouse), inventoryClose())) {
                view_.closeNpcShop();
                session_.submit(EndNpcConversation{ui.dialogueObject});
            } else if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                       session_.content().stashLayout.expansion &&
                       weaponTabAt(input.mouse).has_value()) {
                if (*weaponTabAt(input.mouse) != session_.state().player.weaponSet)
                    session_.submit(SwitchWeaponSet{});
            } else if (input.enter && ui.shopConfirm) {
                const auto slot = *ui.shopConfirm;
                ui.shopConfirm.reset();
                session_.submit(BuyVendorItem{ui.dialogueObject, slot, ui.shopGamble});
            } else if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                       CheckCollisionPointRec(rv(input.mouse), inventoryBounds())) {
                if (const auto *item = saleItemAt()) {
                    if (ui.shopRepair)
                        session_.submit(RepairVendorItem{ui.dialogueObject, item->handle()});
                    else if (session_.vendorSaleQuote(ui.dialogueObject, item->handle()))
                        ui.shopSaleConfirm = item->handle();
                    else
                        view_.notice("That item cannot be sold here.", true);
                }
            } else if (input.insideViewport && input.leftPressed) {
                if (auto slot = view_.clickNpcShop(input.mouse))
                    session_.submit(BuyVendorItem{ui.dialogueObject, *slot, ui.shopGamble});
                if (!ui.shopOpen)
                    session_.submit(EndNpcConversation{ui.dialogueObject});
            } else if (input.insideViewport && input.rightPressed) {
                inventoryRight_ = true;
                if (CheckCollisionPointRec(rv(input.mouse), inventoryBounds())) {
                    if (const auto *item = saleItemAt()) {
                        if (ui.shopRepair)
                            session_.submit(RepairVendorItem{ui.dialogueObject, item->handle()});
                        else
                            session_.submit(SellVendorItem{ui.dialogueObject, item->handle()});
                    }
                } else if (auto slot = view_.clickNpcShop(input.mouse, true))
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
        inventoryClick_ = input.leftHeld || input.leftReleased;
        inventoryRight_ = input.rightHeld;
        return true;
    }
    if (input.character && !ui.blocksWorld()) {
        ui.characterOpen = !ui.characterOpen;
        if (ui.characterOpen) ui.questOpen = ui.hirelingOpen = false;
        return true;
    }
    if (input.hireling && !ui.blocksWorld()) {
        ui.hirelingOpen = !ui.hirelingOpen && session_.state().player.hireling.sourceRow >= 0;
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
        ui.characterOpen = true;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksWorld() &&
        session_.state().player.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(input.mouse), hudSkillTreeButton())) {
        ui.skillTreeOpen = true;
        ui.skillPicker.reset();
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
                if (*button == 4 || *button == 6) {
                    view_.notice("This panel action is not available yet.", true);
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
        else if (ui.inventory.drag || ui.inventory.split)
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
    if (leftCombatTarget_ || rightCombatTarget_) {
        if (handleSkills(input)) return true;
        for (int column = 0; column < 4; ++column)
            if (input.belt[column]) session_.submit(UseBeltColumn{column});
        const bool right = bool(rightCombatTarget_);
        const auto target = right ? rightCombatTarget_ : leftCombatTarget_;
        const auto skill = right ? ui.rightSkill : ui.leftSkill;
        const auto &enemies = session_.state().area.enemies;
        const auto found = std::find_if(enemies.begin(), enemies.end(),
            [&](const Enemy &enemy) { return enemy.id == target && enemy.hp > 0; });
        if (found != enemies.end() && session_.active(found->pos) &&
            skill == (right ? rightTargetSkill_ : leftTargetSkill_)) {
            if (skill) session_.submit(UseClassSkill{*skill, found->pos, target});
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
                    const auto &d = *session_.inventory().catalog().find(item->definition);
                    ui.inventory.drag = InventoryDrag{item->handle(), {}, input.mouse,
                        {d.width * inventoryCellSize / 2, d.height * inventoryCellSize / 2}};
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
                        ui.questSelected = index;
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
    if (!ui.inventory.drag && !ui.inventory.split)
        for (int i = 0; i < 4; ++i)
            if (input.belt[i])
                session_.submit(UseBeltColumn{i});
    if (handleSkills(input) || handleInventory(input))
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
    if (!hudSurface(input.mouse)) {
        if (input.leftPressed || (input.leftHeld && !pickupClick_ && repeatClick_ <= 0)) {
            if (input.shift && ui.leftSkill)
                session_.submit(UseClassSkill{*ui.leftSkill, view_.world(input.mouse), {}});
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
        const bool channeled = rightSkill && rightSkill->originalEffect &&
            rightSkill->originalEffect->effect == Skill::Inferno;
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
                session_.submit(UseClassSkill{*ui.rightSkill, aim, target});
                if (channeled) channelInputSkill_ = *ui.rightSkill;
            } else if (target)
                session_.submit(Attack{target});
        }
    }
    return true;
}
} // namespace d2x
