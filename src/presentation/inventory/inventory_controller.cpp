#include "client/actor_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>
#include <charconv>

namespace d2x {
void SceneController::toggleInventory() {
    if (view_.inventoryView().backpackLayout.cellSize <= 0) {
        view_.notice("Inventory layout is unavailable for this client profile.", true);
        return;
    }
    auto &ui = view_.ui();
    if(ui.orificeObject && ui.inventory.open) {inventoryClient_.submitStaff(ui.orificeObject,{});ui.orificeObject={};ui.orificeItem.reset();}
    ui.skillPicker.reset();
    ui.skillTreeOpen = false;
    if (!ui.inventory.open && !view_.inventoryView().container(view_.inventoryView().containers.backpack)) {
        view_.notice("Inventory is not available in this game.", true);
        return;
    }
    if (!ui.inventory.open && view_.inventoryView().dead) {
        view_.notice("Recover before opening your inventory.", true);
        return;
    }
    if (ui.inventory.storage) {
        inventoryClient_.submit(CloseStorage{});
        ui.inventory.storage = {};
        ui.inventory.open = false;
    } else if (ui.inventory.cubeOpen) {
        inventoryClient_.submit(CloseStorage{});
        ui.inventory.cubeOpen = false;
        ui.inventory.open = false;
    } else
        ui.inventory.open = !ui.inventory.open;
    ui.inventory.cancelGesture();
    ui.help = ui.travelMenu = false;
    if (ui.inventory.open) {
        view_.cancelNpcDialogue();
    }
}
bool SceneController::queueInventory(InventoryIntent command, EntityId source) {
    auto &ui = view_.ui().inventory;
    if (ui.pending)
        return false;
    auto error = inventoryClient_.preview(command);
    if (error != InventoryError::None) {
        view_.notice(inventoryErrorText(error), true);
        return false;
    }
    ui.pending = source;
    ui.pendingPlacement.reset();
    if (ui.drag && ui.drag->item.id==source)
        if (const auto *item=view_.inventoryView().item(source)) {
            const auto *move = std::get_if<MoveItem>(&command);
            const auto *target = move ? std::get_if<ContainerLocation>(&move->destination) : nullptr;
            ui.pendingPlacement=InventoryPendingPlacement{source,item->location,
                target && target->container == view_.inventoryView().containers.cursor};
        }
    if (std::holds_alternative<SwapItems>(command))
        ui.pendingMessage = "Items swapped.";
    else if (std::holds_alternative<SplitStack>(command))
        ui.pendingMessage = "Stack split.";
    else if (std::holds_alternative<MergeStacks>(command))
        ui.pendingMessage = "Stacks merged.";
    else if (std::holds_alternative<LoadBook>(command))
        ui.pendingMessage = "Tome pages updated.";
    else if (std::holds_alternative<SocketItem>(command))
        ui.pendingMessage = "Item socketed.";
    else if (std::holds_alternative<IdentifyItem>(command))
        ui.pendingMessage = "Item identified.";
    else if (std::holds_alternative<EquipBelt>(command))
        ui.pendingMessage = "Belt equipment changed.";
    else if (std::holds_alternative<EquipItem>(command) || std::holds_alternative<EquipHirelingItem>(command))
        ui.pendingMessage = "Equipment changed.";
    else if (std::holds_alternative<TransferItem>(command))
        ui.pendingMessage = "Item transferred.";
    else if (std::holds_alternative<UseItem>(command) || std::holds_alternative<UseHirelingPotion>(command))
        ui.pendingMessage = "Item used.";
    else {
        const auto &move = std::get<MoveItem>(command);
        ui.pendingMessage = std::holds_alternative<GroundLocation>(move.destination)
                                ? "Item dropped on the ground."
                                : "Item moved.";
    }
    inventoryClient_.submit(std::move(command));
    return true;
}
bool SceneController::handleInventory(const FrameInput &input) {
    auto &ui = view_.ui().inventory;
    const auto &inventory = view_.inventoryView();
    if (!input.leftHeld || !input.focused) ui.storagePressedButton.reset();
    if (ui.goldDialog) {
        if (input.escape || input.rightPressed || (input.leftPressed && input.insideViewport &&
                                   CheckCollisionPointRec(rv(input.mouse), goldDialogButton(1)))) {
            ui.goldDialog.reset();
            inventoryClick_ = true;
            inventoryRight_ = input.rightPressed;
            return true;
        }
        auto &entry = *ui.goldDialog;
        const auto eraseSelection = [&] {
            if (entry.caret == entry.anchor) return false;
            const auto first = std::min(entry.caret, entry.anchor);
            entry.amount.erase(first, std::max(entry.caret, entry.anchor) - first);
            entry.caret = entry.anchor = first;
            return true;
        };
        if (input.entrySelectAll) { entry.anchor = 0; entry.caret = entry.amount.size(); }
        if (input.entryHome || input.entryEnd || input.entryStep) {
            if (input.entryHome) entry.caret = 0;
            else if (input.entryEnd) entry.caret = entry.amount.size();
            else if (!input.shift && entry.caret != entry.anchor)
                entry.caret = input.entryStep < 0 ? std::min(entry.caret,entry.anchor) : std::max(entry.caret,entry.anchor);
            else entry.caret = size_t(std::clamp(int(entry.caret) + input.entryStep, 0, int(entry.amount.size())));
            if (!input.shift) entry.anchor = entry.caret;
        }
        if (input.leftPressed && CheckCollisionPointRec(rv(input.mouse),goldDialogEntry())) {
            entry.caret = view_.goldDialogCaretAt(input.mouse);
            if (!input.shift) entry.anchor = entry.caret;
            entry.selecting = true;
        } else if (entry.selecting && (input.leftHeld || input.leftReleased))
            entry.caret = view_.goldDialogCaretAt(input.mouse);
        if (!input.leftHeld) entry.selecting = false;
        if (input.backspace && !eraseSelection() && entry.caret) {
            entry.amount.erase(--entry.caret,1); entry.anchor = entry.caret;
        }
        if (input.entryDelete && !eraseSelection() && entry.caret < entry.amount.size())
            entry.amount.erase(entry.caret,1);
        for (char digit : input.text)
            if (digit >= '0' && digit <= '9') {
                eraseSelection();
                if (entry.amount.size() < 10) {
                    entry.amount.insert(entry.caret++,1,digit); entry.anchor = entry.caret;
                }
            }
        if (input.enter || (input.leftPressed && input.insideViewport &&
                            CheckCollisionPointRec(rv(input.mouse), goldDialogButton(0)))) {
            unsigned amount = 0;
            const auto &digits = ui.goldDialog->amount;
            auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), amount);
            if (error != std::errc{} || end != digits.data() + digits.size() ||
                !amount || amount > ui.goldDialog->maximum)
                view_.notice("Enter a gold amount within the available limit.", true);
            else {
                inventoryClient_.submit(GoldTransaction{ui.goldDialog->action, amount});
                ui.goldDialog.reset();
            }
        }
        inventoryClick_ = input.leftPressed || inventoryClick_;
        return true;
    }
    auto &panel=view_.ui();
    if(panel.orificeObject) {
        if(panel.orificeItem) {const auto *item=inventory.item(panel.orificeItem->id);if(!item || item->revision!=panel.orificeItem->revision) panel.orificeItem.reset();}
        if(input.escape || (input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse),orificeButton(false)))) {
            inventoryClient_.submitStaff(panel.orificeObject,{});panel.orificeItem.reset();inventoryClick_=true;return true;
        }
        if(input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse),orificeButton(true))) {
            if(panel.orificeItem && !inventoryClient_.submitStaff(panel.orificeObject,panel.orificeItem)) view_.notice(inventory.pd2Sacrifice?"The sacrifice request could not be queued.":"The staff request could not be queued.",true);
            inventoryClick_=true;return true;
        }
        if(input.insideViewport && CheckCollisionPointRec(rv(input.mouse),orificeSlot()) && (input.leftPressed || input.leftReleased)) {
            if(ui.drag) {
                const auto *item=inventory.item(ui.drag->item.id);
                const auto *at=item?std::get_if<ContainerLocation>(&item->location):nullptr;
                const bool eligible=item && (inventory.pd2Sacrifice?
                    std::find(inventory.pd2SacrificeItems.begin(),inventory.pd2SacrificeItems.end(),item->id)!=inventory.pd2SacrificeItems.end():
                    item->definition==inventory.staffRecipeOutput);
                if(eligible && at && at->container==inventory.containers.cursor && item->revision==ui.drag->item.revision) {panel.orificeItem=item->handle();ui.drag.reset();}
                else view_.notice(inventory.pd2Sacrifice?"Select an identified, corrupted amulet without desecration or a mirror.":"Only the complete Horadric Staff fits here.",true);
            } else if(input.leftPressed && panel.orificeItem) {
                panel.orificeItem.reset();ui.syncCursor(inventory);
            }
            inventoryClick_=true;return true;
        }
        if(input.insideViewport && CheckCollisionPointRec(rv(input.mouse),orificeBounds())) return true;
    }
    if (inventory.personalItemActionsOnly && ui.storage && view_.storageNavigationReady() &&
        input.insideViewport && input.leftPressed) {
        for (unsigned button = 0; button < 6; ++button) {
            if (button == 5 && !view_.storageMaterialsReady()) continue;
            if (!CheckCollisionPointRec(rv(input.mouse), storageNavigationButton(button, inventory))) continue;
            inventoryClick_ = true;
            ui.storagePressedButton = button;
            if (!ui.drag && !ui.pending && !inventory.storagePagePending) {
                bool accepted = false;
                if (button == 0) accepted = inventoryClient_.selectSharedStorage(!inventory.storageShared);
                else if (button == 5) accepted = input.control ? inventoryClient_.depositAllStorageMaterials() :
                    inventoryClient_.selectStorageMaterials(!inventory.storageMaterialsOpen);
                else if (inventory.storageCnConfirmed) {
                    if (button == 1 || button == 4) {
                        const auto nav = button == 1 ? (input.shift ? StorageNavigation::PreviousMainIndex : StorageNavigation::PreviousIndex) :
                            (input.shift ? StorageNavigation::NextMainIndex : StorageNavigation::NextIndex);
                        accepted = inventoryClient_.navigateStorage(nav);
                    } else if (input.shift)
                        accepted = inventoryClient_.navigateStorage(button == 2 ? StorageNavigation::First : StorageNavigation::LastNonempty);
                    else if (button != 2 || inventory.storagePage > 1)
                        accepted = inventoryClient_.selectStoragePage(button == 2 ? inventory.storagePage - 1 : inventory.storagePage + 1);
                }
                if (!accepted) view_.notice("Storage page is unavailable.", true);
            }
            return true;
        }
    }
    if (inventory.personalItemActionsOnly && inventory.storageCnConfirmed && !inventory.storageMaterialsOpen &&
        ui.storage && !ui.drag && !ui.pending && !inventory.storagePagePending && !inventory.readOnly &&
        input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse), storageNameField(inventory))) {
        inventoryClick_ = true;
        if (input.shift) {
            view_.notice("Stash rename is unavailable on the current CN service.", true);
        } else if (!inventoryClient_.markStorageIndex((inventory.storageIndex + 1) % 3))
            view_.notice("Stash page marking is unavailable or pending.", true);
        return true;
    }
    if (inventory.storageMaterialsOpen && ui.storage && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), storageBounds(inventory))) {
        const bool releasingMaterial = input.leftReleased && ui.drag && ui.drag->moved;
        const bool deferredMaterial = ui.drag && ui.drag->releasedAt && !ui.pending &&
            CheckCollisionPointRec(rv(*ui.drag->releasedAt), storageBounds(inventory));
        if ((input.leftPressed || releasingMaterial || deferredMaterial) && ui.drag) {
            inventoryClick_ = true;
            if (ui.pending && releasingMaterial) ui.drag->releasedAt = input.mouse;
            else if (!ui.pending) {
                ui.drag->releasedAt.reset();
                if (const auto *cursor = inventory.cursorItem())
                    if (!inventoryClient_.depositStorageMaterial(cursor->handle()))
                        view_.notice("This item cannot be deposited in materials.", true);
            }
            return true;
        }
        if (input.leftPressed || input.rightPressed) {
            inventoryClick_ = input.leftPressed || inventoryClick_;
            inventoryRight_ = input.rightPressed || inventoryRight_;
            if (!ui.drag && !ui.pending) for (const auto &material : inventory.storageMaterials) {
                if (!CheckCollisionPointRec(rv(input.mouse), storageMaterialBounds(material))) continue;
                if (input.leftPressed && !material.upgradeSource.empty() && view_.storageMaterialUpgradeReady() &&
                    CheckCollisionPointRec(rv(input.mouse), storageMaterialUpgradeBounds(material))) {
                    const auto amount = input.control && !input.shift ? 15u : input.shift && !input.control ? 150u : 3u;
                    if (!inventoryClient_.upgradeStorageMaterial(material.upgradeSource, amount))
                        view_.notice("Material conversion is unavailable or pending.", true);
                    break;
                }
                if (!material.quantity) break;
                unsigned amount = input.leftPressed ? 1 : input.shift ? 50 : input.control ? 5 : 1;
                if (amount == 50 && material.code.starts_with("rv")) amount = 20;
                const bool accepted = input.control && input.shift ? inventoryClient_.storeMaterialInCube(material.code, amount) :
                    inventoryClient_.withdrawStorageMaterial(material.code, amount, input.leftPressed);
                if (!accepted)
                    view_.notice("Material withdrawal is unavailable or pending.", true);
                break;
            }
        }
        return true;
    }
    if (inventory.personalItemActionsOnly && ui.storage && view_.storageNavigationReady() &&
        input.insideViewport && input.mouseWheel && CheckCollisionPointRec(rv(input.mouse), storageBounds(inventory))) {
        if (!inventory.storageMaterialsOpen && !ui.drag && !ui.pending && !inventory.storagePagePending && inventory.storageCnConfirmed) {
            const auto page = inventory.storagePage;
            if (input.mouseWheel <= 0 || page > 1)
                if (!inventoryClient_.selectStoragePage(input.mouseWheel > 0 ? page - 1 : page + 1))
                    view_.notice("Storage page is unavailable.", true);
        }
        return true;
    }
    EntityId backpack = view_.inventoryView().containers.backpack;
    if (!inventory.readOnly && ui.open && !ui.playerTradeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), inventoryClose(inventory))) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (ui.storage && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), storageClose(inventory))) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (!inventory.readOnly && ui.open && !ui.playerTradeOpen && !ui.drag && !ui.goldDialog && input.insideViewport &&
        input.leftPressed && inventory.stashLayout.expansion)
        if (auto set = weaponTabAt(input.mouse)) {
            inventoryClick_ = true;
            if (*set != inventory.weaponSet)
                inventoryClient_.submit(SwitchWeaponSet{});
            return true;
        }
    if (ui.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), cubeClose(inventory))) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (ui.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), cubeTransmute())) {
        inventoryClick_ = true;
        if (!ui.drag && !ui.pending && !inventory.readOnly && !inventory.itemOperationPending)
            inventoryClient_.submit(TransmuteCube{});
        return true;
    }
    bool bankField = ui.storage && CheckCollisionPointRec(
            rv(input.mouse), storageGoldButton(inventory));
    const bool goldActions = !inventory.personalItemActionsOnly || (inventory.goldKnown &&
        (!ui.storage || (inventory.storageCnConfirmed && !inventory.storageShared &&
            !inventory.storageMaterialsOpen && inventory.storagePagingAvailable && !inventory.storagePagePending)));
    if (!inventory.readOnly && goldActions && ui.open && !ui.playerTradeOpen && input.insideViewport && input.leftPressed && !ui.pending &&
        !ui.drag && (CheckCollisionPointRec(rv(input.mouse), inventoryGoldButton(inventory)) || bankField)) {
        GoldAction action = bankField ? GoldAction::Withdraw :
                            ui.storage ? GoldAction::Deposit : GoldAction::Drop;
        const auto &player = inventory;
        // CN may omit an initial zero bank stat. A deposit is bounded by the
        // known wallet; the server checks unknown destination capacity.
        unsigned maximum = action == GoldAction::Withdraw
            ? (player.bankGoldKnown ? std::min(player.bankGold, (inventory.walletLimit > player.gold ? inventory.walletLimit - player.gold : 0)) : 0)
            : action == GoldAction::Deposit
                ? (inventory.personalItemActionsOnly && !player.bankGoldKnown ? player.gold :
                    std::min(player.gold, (inventory.bankGoldLimit > player.bankGold ? inventory.bankGoldLimit - player.bankGold : 0)))
                : std::min(player.gold, inventory.groundGoldLimit);
        if (!maximum)
            view_.notice("No gold can be transferred here.", true);
        else
            ui.goldDialog = GoldDialog{action, std::to_string(maximum), maximum};
        inventoryClick_ = true;
        return true;
    }
    if (ui.identify) {
        const auto *source = inventory.item(ui.identify->id);
        if (!source || source->revision != ui.identify->revision) {
            ui.identify.reset();
            view_.notice("That scroll or tome has changed.", true);
            return true;
        }
        if (input.rightPressed) {
            ActorControlIntent intent; intent.action = ActorControlIntent::Action::CancelTargeting;
            actorClient_.control(intent);
            inventoryRight_ = true;
            ui.identify.reset();
            return true;
        }
        if (input.leftPressed && input.insideViewport) {
            inventoryClick_ = true;
            EntityId target;
            for (const auto &grid : inventoryGrids(inventory, ui))
                if (auto cell = grid.cellAt(input.mouse)) {
                    target = inventory.itemAt(grid.container, *cell);
                    break;
                }
            if (!target)
                if (auto slot = equipmentAt(inventory, input.mouse, inventory.weaponSet))
                    target = inventory.equipped(view_.inventoryView().containers, *slot);
            if (const auto *item = inventory.item(target))
                if (queueInventory(IdentifyItem{*ui.identify, item->handle()}, source->id))
                    ui.identify.reset();
        }
        return true;
    }
    if (ui.drag) {
        auto *source = inventory.item(ui.drag->item.id);
        if (!source || source->revision != ui.drag->item.revision) {
            ui.drag.reset();
            view_.notice("That item has changed. Select it again.", true);
            return true;
        }
        if (input.rightPressed) {
            inventoryRight_ = true;
            return true;
        }
        if (ui.drag->releasedAt && !ui.pending) {
            const auto mouse = *ui.drag->releasedAt;
            ui.drag->releasedAt.reset();
            if (ui.drag->onCursor) {
                auto drop = view_.inventoryDropAt(mouse);
                if (drop.command) queueInventory(std::move(*drop.command),source->id);
                else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error),true);
            }
            return true;
        }
        if (ui.drag->pickedUp) {
            if (input.leftPressed && input.insideViewport && !ui.pending) {
                inventoryClick_ = true;
                auto drop = view_.inventoryDropAt(input.mouse);
                if (drop.command) {
                    if (queueInventory(std::move(*drop.command), source->id))
                        if(!ui.drag->onCursor) ui.drag.reset();
                } else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error), true);
            }
            return true;
        }
        if ((input.mouse - ui.drag->pressedAt).length() > 4)
            ui.drag->moved = true;
        if (input.leftReleased) {
            if (ui.drag->moved && input.insideViewport && ui.pending) ui.drag->releasedAt = input.mouse;
            if (ui.drag->moved && input.insideViewport && !ui.pending) {
                auto drop = view_.inventoryDropAt(input.mouse);
                if (drop.command) {
                    if (queueInventory(std::move(*drop.command), source->id)) {
                        if(!ui.drag->onCursor) ui.drag.reset();
                        return true;
                    }
                }
                else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error), true);
            }
            ui.drag->pickedUp = true;
            ui.drag->moved = true;
        } else if (!input.leftHeld) {
            ui.drag->pickedUp = true;
            ui.drag->moved = true;
        }
        return true;
    }
    const auto &containers = view_.inventoryView().containers;
    const auto *belt = inventory.container(containers.belt);
    int rows = belt && (ui.open || ui.beltExpanded || ui.beltHovered) ? belt->rows : 1;
    auto grids = inventoryGrids(inventory, ui);
    const ContainerGrid *hitGrid = nullptr;
    std::optional<Cell> cell;
    for (const auto &grid : grids) {
        cell = grid.cellAt(input.mouse);
        if (cell) {
            hitGrid = &grid;
            break;
        }
    }
    auto equipmentSlot = ui.open && !ui.playerTradeOpen ? equipmentAt(inventory, input.mouse, inventory.weaponSet)
                                 : std::nullopt;
    bool equipment = equipmentSlot.has_value();
    bool inBelt = belt && CheckCollisionPointRec(rv(input.mouse), beltBounds(rows));
    if (!input.insideViewport || (!inBelt && !inventorySurface(ui, input.mouse)))
        return inventoryClick_;
    if (input.rightHeld)
        inventoryRight_ = true;
    EntityId hovered = hitGrid     ? inventory.itemAt(hitGrid->container, *cell)
                       : equipment ? inventory.equipped(containers, *equipmentSlot)
                                   : EntityId{};
    if (view_.ui().inventoryQuestNpc) {
        if (input.leftPressed) {
            if (const auto *item = inventory.item(hovered))
                submitNpcItemService(item->handle());
            inventoryClick_ = true;
        }
        return true;
    }
    auto useItem = [&](const InventoryItemView &item) {
        const auto &definition = *inventory.definition(item.definition);
        if (!definition.usable && !definition.readableQuestClue) return true;
        if (definition.opensCube) {
            if (view_.ui().shopOpen) {
                const auto npc = view_.ui().dialogueObject;
                view_.closeNpcShop();
                endInventoryNpcConversation(npc);
            }
            if (ui.cubeOpen) inventoryClient_.submit(CloseStorage{});
            else queueInventory(UseItem{item.handle()},item.id);
            ui.cubeOpen = false;
            ui.open = true;
            return true;
        }
        if (definition.identifySource && definition.bookCapacity && !item.charges) return true;
        return queueInventory(UseItem{item.handle()},item.id);
    };
    if (input.rightPressed && !ui.pending) {
        if (ui.playerTradeOpen) return true;
        if (auto item = inventory.item(hovered)) {
            if (input.shift && hitGrid && hitGrid->container == containers.belt)
                queueInventory(UseHirelingPotion{item->handle()}, item->id);
            else
                useItem(*item);
        } else
            ui.selected = {};
        return true;
    }
    if (!input.leftPressed)
        return true;
    inventoryClick_ = true;
    if (ui.pending || view_.inventoryView().dead)
        return true;
    if (hitGrid || equipment) {
        ui.selected = hovered;
        if (const auto *item = inventory.item(hovered)) {
            auto def = inventory.definition(item->definition);
            if (input.shift && !ui.playerTradeOpen && hitGrid && hitGrid->container == backpack && def->beltAllowed) {
                if (inventoryClient_.beltSpace(def->code))
                    queueInventory(TransferItem{item->handle(), containers.belt}, item->id);
            } else if (!input.leftReleased) {
                ui.drag = InventoryDrag{item->handle(), input.mouse};
                if (!queueInventory(MoveItem{item->handle(), ContainerLocation{containers.cursor,{}}}, item->id))
                    ui.drag.reset();
            }
        }
        return true;
    }
    return true;
}
} // namespace d2x
