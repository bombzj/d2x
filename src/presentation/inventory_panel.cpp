#include "inventory_panel.hpp"
#include <algorithm>

namespace d2x {
std::optional<Cell> inventoryCell(Vec mouse) {
    auto grid = inventoryGrid();
    if (!CheckCollisionPointRec(rv(mouse), grid))
        return std::nullopt;
    return Cell{int((mouse.x - grid.x) / inventoryCellSize), int((mouse.y - grid.y) / inventoryCellSize)};
}
Rectangle inventoryItemBounds(Cell cell, const ItemDefinition &definition) {
    auto grid = inventoryGrid();
    return {grid.x + cell.x * inventoryCellSize, grid.y + cell.y * inventoryCellSize,
            definition.width * inventoryCellSize, definition.height * inventoryCellSize};
}
std::vector<ContainerGrid> inventoryGrids(const GameSession &session, const InventoryUi &ui) {
    const auto &c = session.playerContainers();
    int rows = ui.open || ui.beltExpanded ? session.inventory().container(c.belt)->spec.rows : 1;
    auto belt = beltSlot({0, 0});
    std::vector<ContainerGrid> grids{
        {c.belt, {belt.x, belt.y}, {31 * hudScale, -32 * hudScale}, 4, rows, 29 * hudScale}};
    if (ui.open) {
        auto p = inventoryGrid();
        grids.push_back(
            {c.backpack, {p.x, p.y}, {inventoryCellSize, inventoryCellSize}, 10, 4, inventoryCellSize});
    }
    if (ui.storage && session.storage().container == ui.storage) {
        auto p = storageBounds();
        const auto &spec = session.inventory().container(ui.storage)->spec;
        grids.push_back({ui.storage,
                         {p.x + 74 * inventoryScale, p.y + 273 * inventoryScale},
                         {inventoryCellSize, inventoryCellSize},
                         spec.columns,
                         spec.rows,
                         inventoryCellSize});
    }
    return grids;
}
bool inventorySurface(const InventoryUi &ui, Vec mouse) {
    return (ui.open && CheckCollisionPointRec(rv(mouse), inventoryBounds())) ||
           (ui.storage && CheckCollisionPointRec(rv(mouse), storageBounds()));
}
InventoryDrop inventoryDrop(const GameSession &session, const InventoryUi &ui, Vec mouse) {
    InventoryDrop drop;
    if (!ui.drag)
        return drop;
    const auto &inventory = session.inventory();
    const auto *source = inventory.item(ui.drag->item.id);
    if (!source || source->revision != ui.drag->item.revision) {
        drop.error = source ? InventoryError::SourceChanged : InventoryError::UnknownItem;
        drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    auto grids = inventoryGrids(session, ui);
    auto location = std::get_if<ContainerLocation>(&source->location);
    auto sourceGrid = std::find_if(grids.begin(), grids.end(), [&](const auto &grid) {
        return location && grid.container == location->container;
    });
    if (sourceGrid == grids.end()) {
        drop.error = InventoryError::AccessDenied;
        drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    const auto &definition = *inventory.catalog().find(source->definition);
    if (ui.open && definition.beltRows && CheckCollisionPointRec(rv(mouse), equippedBeltBounds())) {
        drop.command = EquipBelt{source->handle()};
        drop.bounds = equippedBeltBounds();
        drop.description = "Equip belt";
    } else {
        for (const auto &grid : grids) {
            auto cell = grid.cellAt(mouse);
            if (!cell)
                continue;
            Cell origin{cell->x - ui.drag->grab.x, cell->y - ui.drag->grab.y};
            drop.bounds = grid.itemBounds(origin, definition);
            auto target = inventory.item(inventory.itemAt(grid.container, *cell));
            if (target && target->id != source->id) {
                auto targetCell = std::get<ContainerLocation>(target->location).cell;
                drop.bounds = grid.itemBounds(targetCell, definition);
                if (!ui.forceSwap && definition.maxStack > 1 && target->definition == source->definition) {
                    drop.command = MergeStacks{source->handle(), target->handle()};
                    drop.description = "Merge into this stack";
                } else {
                    drop.command = SwapItems{source->handle(), target->handle()};
                    drop.description = "Swap both items";
                    drop.otherBounds =
                        sourceGrid->itemBounds(location->cell, *inventory.catalog().find(target->definition));
                }
            } else {
                drop.command = MoveItem{source->handle(), ContainerLocation{grid.container, origin}};
                drop.description = "Move item";
            }
            break;
        }
    }
    if (!drop.command) {
        if (ui.open && CheckCollisionPointRec(rv(mouse), inventoryButton(0))) {
            drop.command = MoveItem{source->handle(), AutoPlace{session.playerContainers().backpack}};
            drop.description = "Place in backpack";
        } else if (ui.storage && CheckCollisionPointRec(rv(mouse), storageTransfer())) {
            EntityId target =
                location->container == ui.storage ? session.playerContainers().backpack : ui.storage;
            drop.command = TransferItem{source->handle(), target};
            drop.description = "Transfer whole item";
        } else if (!inventorySurface(ui, mouse) && mouse.x >= 0 && mouse.x < W && mouse.y >= 0 &&
                   !hudSurface(mouse)) {
            if (auto ground = session.dropLocation()) {
                drop.command = MoveItem{source->handle(), *ground};
                drop.description = "Drop at your feet";
            } else
                drop.error = InventoryError::InvalidLocation;
        } else
            drop.description = "Release to cancel";
    }
    if (drop.command)
        drop.error = session.previewInventory(*drop.command);
    if (drop.error != InventoryError::None)
        drop.description = inventoryErrorText(drop.error);
    return drop;
}
} // namespace d2x
