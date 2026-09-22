#include "controller.hpp"
#include <algorithm>

namespace d2x {
void SceneController::toggleInventory() {
    auto &ui = view_.ui();
    if (!ui.inventory.open && session_.state().player.dead) {
        view_.notice("Recover before opening your inventory.", true);
        return;
    }
    if (ui.inventory.storage) {
        session_.submit(CloseStorage{});
        ui.inventory.storage = {};
        ui.inventory.open = false;
    } else
        ui.inventory.open = !ui.inventory.open;
    ui.inventory.cancelGesture();
    ui.help = ui.travelMenu = false;
    if (ui.inventory.open) {
        ui.dialogue.clear();
        ui.clickAge = 10;
        session_.submit(StopMoving{});
    }
}
bool SceneController::queueInventory(GameCommand command, EntityId source) {
    auto &ui = view_.ui().inventory;
    if (ui.pending)
        return false;
    auto error = session_.previewInventory(command);
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
    else if (std::holds_alternative<EquipBelt>(command))
        ui.pendingMessage = "Belt equipment changed.";
    else if (std::holds_alternative<TransferItem>(command))
        ui.pendingMessage = "Item transferred.";
    else if (std::holds_alternative<UseItem>(command))
        ui.pendingMessage = "Potion used.";
    else {
        const auto &move = std::get<MoveItem>(command);
        ui.pendingMessage = std::holds_alternative<GroundLocation>(move.destination)
                                ? "Item dropped on the ground."
                                : "Item moved.";
    }
    session_.submit(std::move(command));
    return true;
}
bool SceneController::handleInventory(const FrameInput &input) {
    auto &ui = view_.ui().inventory;
    ui.forceSwap = input.control;
    const auto &inventory = session_.inventory();
    EntityId backpack = session_.playerContainers().backpack;
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
    if (ui.drag) {
        auto *source = inventory.item(ui.drag->item.id);
        if (!source || source->revision != ui.drag->item.revision) {
            ui.drag.reset();
            view_.notice("That item has changed. Select it again.", true);
            return true;
        }
        if (input.rightPressed) {
            inventoryRight_ = true;
            ui.drag.reset();
            return true;
        }
        if ((input.mouse - ui.drag->pressedAt).length() > 4)
            ui.drag->moved = true;
        if (input.leftReleased) {
            if (ui.drag->moved && input.insideViewport) {
                auto drop = inventoryDrop(session_, ui, input.mouse);
                if (drop.command)
                    queueInventory(std::move(*drop.command), source->id);
                else if (drop.error != InventoryError::None)
                    view_.notice(inventoryErrorText(drop.error), true);
            }
            ui.drag.reset();
        } else if (!input.leftHeld)
            ui.drag.reset();
        return true;
    }
    const auto &containers = session_.playerContainers();
    int rows = ui.open || ui.beltExpanded ? inventory.container(containers.belt)->spec.rows : 1;
    auto grids = inventoryGrids(session_, ui);
    const ContainerGrid *hitGrid = nullptr;
    std::optional<Cell> cell;
    for (const auto &grid : grids) {
        cell = grid.cellAt(input.mouse);
        if (cell) {
            hitGrid = &grid;
            break;
        }
    }
    bool equipment = ui.open && CheckCollisionPointRec(rv(input.mouse), equippedBeltBounds());
    bool inBelt = CheckCollisionPointRec(rv(input.mouse), beltBounds(rows));
    if (!input.insideViewport || (!inBelt && !inventorySurface(ui, input.mouse)))
        return inventoryClick_;
    if (input.rightHeld)
        inventoryRight_ = true;
    EntityId hovered = hitGrid     ? inventory.itemAt(hitGrid->container, *cell)
                       : equipment ? inventory.itemAt(containers.beltEquipment, {0, 0})
                                   : EntityId{};
    if (input.rightPressed && !ui.pending) {
        if (auto item = inventory.item(hovered)) {
            auto def = inventory.catalog().find(item->definition);
            if (hitGrid && hitGrid->container == ui.storage)
                view_.notice("Move this item to your backpack before using it.", true);
            else if (def->beltRows)
                queueInventory(EquipBelt{item->handle()}, item->id);
            else
                queueInventory(UseItem{item->handle()}, item->id);
        } else
            ui.selected = {};
        return true;
    }
    if (!input.leftPressed)
        return true;
    inventoryClick_ = true;
    if (ui.pending || session_.state().player.dead)
        return true;
    if (hitGrid || equipment) {
        ui.selected = hovered;
        if (const auto *item = inventory.item(hovered)) {
            auto def = inventory.catalog().find(item->definition);
            if (input.shift) {
                if (ui.storage && hitGrid && hitGrid->container != containers.belt) {
                    EntityId target = hitGrid->container == ui.storage ? backpack : ui.storage;
                    queueInventory(TransferItem{item->handle(), target}, item->id);
                } else if (equipment || def->beltRows)
                    queueInventory(EquipBelt{item->handle()}, item->id);
                else if (hitGrid && hitGrid->container == backpack && def->beltAllowed) {
                    auto space = inventory.beltSpace(containers.belt, def->code, false);
                    if (space)
                        queueInventory(MoveItem{item->handle(), ContainerLocation{containers.belt, *space}},
                                       item->id);
                    else
                        view_.notice("No suitable belt column has room.", true);
                } else
                    queueInventory(MoveItem{item->handle(), AutoPlace{backpack}}, item->id);
            } else if (!equipment && !input.leftReleased) {
                const auto &location = std::get<ContainerLocation>(item->location);
                auto bounds = hitGrid->itemBounds(location.cell, *def);
                ui.drag = InventoryDrag{item->handle(),
                                        {cell->x - location.cell.x, cell->y - location.cell.y},
                                        input.mouse,
                                        input.mouse - Vec{bounds.x, bounds.y}};
            }
        }
        return true;
    }
    if (!ui.open)
        return true;
    const auto *item = inventory.item(ui.selected);
    if (!item)
        return true;
    if (ui.storage && CheckCollisionPointRec(rv(input.mouse), storageTransfer())) {
        auto at = std::get_if<ContainerLocation>(&item->location);
        if (at)
            queueInventory(TransferItem{item->handle(), at->container == ui.storage ? backpack : ui.storage},
                           item->id);
    } else if (CheckCollisionPointRec(rv(input.mouse), inventoryButton(0)))
        queueInventory(MoveItem{item->handle(), AutoPlace{backpack}}, item->id);
    else if (CheckCollisionPointRec(rv(input.mouse), inventoryButton(1))) {
        if (item->quantity > 1)
            ui.split = SplitDialog{item->handle(), std::max(1u, item->quantity / 2),
                                   std::get<ContainerLocation>(item->location).container};
    } else if (CheckCollisionPointRec(rv(input.mouse), inventoryButton(2))) {
        if (auto ground = session_.dropLocation())
            queueInventory(MoveItem{item->handle(), *ground}, item->id);
        else
            view_.notice("Cannot drop an item here.", true);
    }
    return true;
}
} // namespace d2x
