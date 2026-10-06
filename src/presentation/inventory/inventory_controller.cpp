#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>
#include <charconv>

namespace d2x {
void SceneController::toggleInventory() {
    auto &ui = view_.ui();
    ui.skillPicker.reset();
    ui.skillTreeOpen = false;
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
    ui.forceSwap = input.control;
    const auto &inventory = view_.inventoryView();
    EntityId backpack = view_.inventoryView().containers.backpack;
    if (ui.open && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), inventoryClose())) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (ui.storage && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), storageClose())) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (ui.open && !ui.drag && !ui.split && !ui.goldDialog && input.insideViewport &&
        input.leftPressed && inventory.stashLayout.expansion)
        if (auto set = weaponTabAt(input.mouse)) {
            inventoryClick_ = true;
            if (*set != inventory.weaponSet)
                inventoryClient_.submit(SwitchWeaponSet{});
            return true;
        }
    if (ui.split) {
        auto *source = inventory.item(ui.split->item.id);
        if (!source || source->revision != ui.split->item.revision) {
            ui.split.reset();
            view_.notice("That stack has changed. Select it again.", true);
            return true;
        }
        if (input.rightPressed) {
            inventoryRight_ = true;
            ui.split.reset();
            return true;
        }
        int delta = input.quantityDelta;
        if (input.leftPressed && input.insideViewport) {
            inventoryClick_ = true;
            if (CheckCollisionPointRec(rv(input.mouse), splitAdjust(0)))
                --delta;
            if (CheckCollisionPointRec(rv(input.mouse), splitAdjust(1)))
                ++delta;
            if (CheckCollisionPointRec(rv(input.mouse), splitButton(1))) {
                ui.split.reset();
                return true;
            }
        }
        ui.split->quantity =
            unsigned(std::clamp(int(ui.split->quantity) + delta, 1, int(source->quantity) - 1));
        if (!ui.pending && (input.enter || (input.leftPressed && input.insideViewport &&
                                            CheckCollisionPointRec(rv(input.mouse), splitButton(0))))) {
            if (queueInventory(
                    SplitStack{ui.split->item, ui.split->quantity, AutoPlace{ui.split->destination}},
                    source->id))
                ui.split.reset();
        }
        return true;
    }
    if (ui.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), cubeClose())) {
        inventoryClick_ = true;
        toggleInventory();
        return true;
    }
    if (ui.cubeOpen && input.insideViewport && input.leftPressed &&
        CheckCollisionPointRec(rv(input.mouse), cubeTransmute())) {
        inventoryClick_ = true;
        inventoryClient_.submit(TransmuteCube{});
        return true;
    }
    if (ui.goldDialog) {
        if (input.rightPressed || (input.leftPressed && input.insideViewport &&
                                   CheckCollisionPointRec(rv(input.mouse), goldDialogButton(1)))) {
            ui.goldDialog.reset();
            inventoryClick_ = true;
            inventoryRight_ = input.rightPressed;
            return true;
        }
        if (input.backspace && !ui.goldDialog->amount.empty())
            ui.goldDialog->amount.pop_back();
        for (char digit : input.text)
            if (ui.goldDialog->amount.size() < 9)
                ui.goldDialog->amount += digit;
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
    bool bankField = ui.storage && CheckCollisionPointRec(
            rv(input.mouse), storageGold(inventory.stashLayout.expansion));
    if (ui.open && input.insideViewport && input.leftPressed && !ui.pending &&
        !ui.drag && (CheckCollisionPointRec(rv(input.mouse), inventoryGold()) || bankField)) {
        GoldAction action = bankField ? GoldAction::Withdraw :
                            ui.storage ? GoldAction::Deposit : GoldAction::Drop;
        const auto &player = inventory;
        unsigned maximum = action == GoldAction::Withdraw
            ? std::min(player.bankGold, inventory.walletLimit - player.gold)
            : action == GoldAction::Deposit
                ? std::min(player.gold, inventory.bankGoldLimit - player.bankGold)
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
                if (auto slot = equipmentAt(input.mouse, inventory.weaponSet))
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
            if (!ui.drag->onCursor) ui.drag.reset();
            return true;
        }
        if (ui.drag->pickedUp) {
            if (input.leftPressed && input.insideViewport && !ui.pending) {
                inventoryClick_ = true;
                auto drop = inventoryDrop(inventory, inventoryClient_, ui, input.mouse, view_.ui().hirelingOpen);
                if (drop.command) {
                    if (queueInventory(std::move(*drop.command), source->id))
                        ui.drag.reset();
                } else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error), true);
            }
            return true;
        }
        if ((input.mouse - ui.drag->pressedAt).length() > 4)
            ui.drag->moved = true;
        if (input.leftReleased) {
            if (ui.drag->moved && input.insideViewport) {
                auto drop = inventoryDrop(inventory, inventoryClient_, ui, input.mouse, view_.ui().hirelingOpen);
                if (drop.command) {
                    if (queueInventory(std::move(*drop.command), source->id)) {
                        ui.drag.reset();
                        return true;
                    }
                }
                else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error), true);
            }
            if (ui.drag->moved && !input.insideViewport)
                ui.drag.reset();
            else {
                const auto &definition = *inventory.definition(source->definition);
                ui.drag->grab = {definition.width / 2, definition.height / 2};
                ui.drag->pixelOffset = {definition.width * inventoryCellSize / 2,
                                        definition.height * inventoryCellSize / 2};
                ui.drag->pickedUp = true;
                ui.drag->moved = true;
            }
        } else if (!input.leftHeld)
            ui.drag.reset();
        return true;
    }
    const auto &containers = view_.inventoryView().containers;
    int rows = ui.open || ui.beltExpanded ? inventory.container(containers.belt)->rows : 1;
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
    auto equipmentSlot = ui.open ? equipmentAt(input.mouse, inventory.weaponSet)
                                 : std::nullopt;
    bool equipment = equipmentSlot.has_value();
    bool inBelt = CheckCollisionPointRec(rv(input.mouse), beltBounds(rows));
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
    auto changeEquipment = [&](const InventoryItemView &item) {
        const auto &definition = *inventory.definition(item.definition);
        if (definition.opensCube) {
            auto location = std::get_if<ContainerLocation>(&item.location);
            if (!location || location->container != backpack)
                view_.notice("Carry the cube in your backpack to open it.", true);
            else {
                if (view_.ui().shopOpen) {
                    const auto npc = view_.ui().dialogueObject;
                    view_.closeNpcShop();
                    endInventoryNpcConversation(npc);
                }
                if (ui.storage) {
                    inventoryClient_.submit(CloseStorage{});
                    ui.storage = {};
                }
                if (ui.cubeOpen) inventoryClient_.submit(CloseStorage{});
                else inventoryClient_.submit(UseItem{item.handle()});
                // The native storage response opens the panel; sending a request
                // alone does not make the cube available.
                ui.cubeOpen = false;
                ui.open = true;
                ui.cancelGesture();
            }
            return true;
        }
        if (definition.identifySource) {
            if (definition.bookCapacity && !item.charges)
                view_.notice("This tome is empty.", true);
            else {
                ui.identify = item.handle();
                ui.open = true;
                view_.notice("Click an unidentified item.");
            }
            return true;
        }
        auto location = std::get_if<ContainerLocation>(&item.location);
        if (definition.beltRows)
            return queueInventory(EquipBelt{item.handle()}, item.id);
        if (location && location->container == containers.equipment)
            return queueInventory(EquipItem{item.handle(), std::nullopt}, item.id);
        std::optional<EquipmentSlot> candidate;
        for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index) {
            auto slot = EquipmentSlot(index);
            if (slot == EquipmentSlot::RightHand)
                slot = weaponHandSlot(false, inventory.weaponSet);
            if (slot == EquipmentSlot::LeftHand)
                slot = weaponHandSlot(true, inventory.weaponSet);
            if (slot == EquipmentSlot::Belt || !definition.fits(slot))
                continue;
            if (!candidate)
                candidate = slot;
            if (!inventory.equipped(containers, slot)) {
                candidate = slot;
                break;
            }
        }
        if (candidate)
            return queueInventory(EquipItem{item.handle(), candidate}, item.id);
        return queueInventory(UseItem{item.handle()}, item.id);
    };
    if (input.rightPressed && !ui.pending) {
        if (auto item = inventory.item(hovered)) {
            if (hitGrid && (hitGrid->container == ui.storage ||
                            hitGrid->container == view_.inventoryView().containers.cube))
                view_.notice("Move this item to your backpack before using it.", true);
            else if (input.shift && hitGrid && hitGrid->container == containers.belt)
                queueInventory(UseHirelingPotion{item->handle()}, item->id);
            else
                changeEquipment(*item);
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
            if (input.control && input.shift && item->quality == ItemQuality::Normal &&
                item->quantity > 1 && !equipment) {
                ui.split = SplitDialog{item->handle(), std::max(1u, item->quantity / 2),
                                       std::get<ContainerLocation>(item->location).container};
            } else if (input.shift) {
                if ((ui.storage || ui.cubeOpen) && hitGrid && hitGrid->container != containers.belt) {
                    EntityId openContainer = ui.storage ? ui.storage : containers.cube;
                    EntityId target = hitGrid->container == openContainer ? backpack : openContainer;
                    queueInventory(TransferItem{item->handle(), target}, item->id);
                } else if (equipment || def->beltRows)
                    changeEquipment(*item);
                else if (hitGrid && hitGrid->container == backpack && def->beltAllowed) {
                    auto space = inventoryClient_.beltSpace(def->code);
                    if (space)
                        queueInventory(MoveItem{item->handle(), ContainerLocation{containers.belt, *space}},
                                       item->id);
                    else
                        view_.notice("No suitable belt column has room.", true);
                } else
                    queueInventory(MoveItem{item->handle(), AutoPlace{backpack}}, item->id);
            } else if (!input.leftReleased) {
                const auto &location = std::get<ContainerLocation>(item->location);
                if (equipment)
                    ui.drag = InventoryDrag{item->handle(), {}, input.mouse,
                                            {def->width * inventoryCellSize / 2,
                                             def->height * inventoryCellSize / 2}};
                else {
                    auto bounds = hitGrid->itemBounds(location.cell, *def);
                    ui.drag = InventoryDrag{item->handle(),
                                            {cell->x - location.cell.x, cell->y - location.cell.y},
                                            input.mouse, input.mouse - Vec{bounds.x, bounds.y}};
                }
            }
        }
        return true;
    }
    return true;
}
} // namespace d2x
