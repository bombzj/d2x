#include "inventory_panel.hpp"
#include "hireling_panel.hpp"
#include <algorithm>

namespace d2x {
Rectangle equipmentBounds(EquipmentSlot slot) {
    if (slot == EquipmentSlot::AlternateRightHand) slot = EquipmentSlot::RightHand;
    if (slot == EquipmentSlot::AlternateLeftHand) slot = EquipmentSlot::LeftHand;
    constexpr std::array<Rectangle, size_t(EquipmentSlot::Count)> bounds{{{135, 8, 54, 51},
                                                                          {209, 35, 23, 24},
                                                                          {133, 77, 56, 82},
                                                                          {20, 47, 55, 112},
                                                                          {251, 47, 55, 112},
                                                                          {95, 180, 23, 24},
                                                                          {209, 180, 23, 24},
                                                                          {136, 179, 52, 25},
                                                                          {252, 182, 54, 52},
                                                                          {21, 181, 54, 53}}};
    if (size_t(slot) >= bounds.size())
        return {};
    auto box = bounds[size_t(slot)];
    auto panel = inventoryBounds();
    return {panel.x + box.x * inventoryScale, panel.y + box.y * inventoryScale, box.width * inventoryScale,
            box.height * inventoryScale};
}
std::optional<EquipmentSlot> equipmentAt(Vec mouse, unsigned weaponSet) {
    for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index)
        if (CheckCollisionPointRec(rv(mouse), equipmentBounds(EquipmentSlot(index)))) {
            auto slot = EquipmentSlot(index);
            return slot == EquipmentSlot::RightHand ? weaponHandSlot(false, weaponSet) :
                   slot == EquipmentSlot::LeftHand ? weaponHandSlot(true, weaponSet) : slot;
        }
    return std::nullopt;
}
Rectangle weaponTabBounds(unsigned set, bool left) {
    auto panel = inventoryBounds();
    return {panel.x + (left ? 248.f : 17.f) * inventoryScale + set * 32.f * inventoryScale,
            panel.y + 23.f * inventoryScale, 30.f * inventoryScale, 20.f * inventoryScale};
}
std::optional<unsigned> weaponTabAt(Vec mouse) {
    for (unsigned set = 0; set < 2; ++set)
        if (CheckCollisionPointRec(rv(mouse), weaponTabBounds(set, false)) ||
            CheckCollisionPointRec(rv(mouse), weaponTabBounds(set, true)))
            return set;
    return std::nullopt;
}
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
        const auto &layout = session.content().stashLayout;
        grids.push_back({ui.storage,
                         {p.x + layout.left * inventoryScale, p.y + layout.top * inventoryScale},
                         {layout.cellSize * inventoryScale, layout.cellSize * inventoryScale},
                         spec.columns,
                         spec.rows,
                         layout.cellSize * inventoryScale});
    }
    if (ui.cubeOpen && c.cube) {
        auto p = cubeBounds();
        const auto &layout = session.content().cubeLayout;
        grids.push_back({c.cube,
                         {p.x + layout.left * inventoryScale, p.y + layout.top * inventoryScale},
                         {layout.cellSize * inventoryScale, layout.cellSize * inventoryScale},
                         layout.columns, layout.rows, layout.cellSize * inventoryScale});
    }
    return grids;
}
void InventoryUi::syncCursor(const GameSession &session) {
    if (const auto *item = session.cursorItem()) {
        if (!drag || !drag->onCursor || drag->item.id != item->id) {
            const auto &definition = *session.inventory().catalog().find(item->definition);
            drag = InventoryDrag{item->handle(), {definition.width / 2, definition.height / 2}, {},
                {definition.width * inventoryCellSize / 2, definition.height * inventoryCellSize / 2},
                true, true, true};
        } else drag->item = item->handle();
        identify.reset();
    } else if (drag && drag->onCursor) drag.reset();
}
bool inventorySurface(const InventoryUi &ui, Vec mouse) {
    return (ui.open && CheckCollisionPointRec(rv(mouse), classicSideBounds(true))) ||
           ((ui.storage || ui.cubeOpen) && CheckCollisionPointRec(rv(mouse), classicSideBounds(false)));
}
InventoryDrop inventoryDrop(const GameSession &session, const InventoryUi &ui, Vec mouse, bool hirelingOpen) {
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
    const auto &containers = session.playerContainers();
    if (hirelingOpen && CheckCollisionPointRec(rv(mouse), classicSideBounds(false))) {
        constexpr EquipmentSlot order[] = {EquipmentSlot::Head, EquipmentSlot::Torso,
                                           EquipmentSlot::RightHand, EquipmentSlot::RightHand};
        const auto &layout = session.content().hirelingLayout;
        drop.description = "Release to cancel";
        for (size_t index = 0; index < layout.slots.size(); ++index) {
            const auto &box = layout.slots[index];
            auto bounds = hirelingArtRect(float(box[0]), float(box[1]),
                                           float(box[2] - box[0]), float(box[3] - box[1]));
            if (!CheckCollisionPointRec(rv(mouse), bounds)) continue;
            drop.bounds = bounds;
            drop.command = EquipHirelingItem{source->handle(), order[index]};
            drop.error = session.previewInventory(*drop.command);
            drop.description = drop.error == InventoryError::None ? "Equip mercenary" :
                                                                   inventoryErrorText(drop.error);
            break;
        }
        return drop;
    }
    bool equipped = location && (location->container == containers.equipment ||
                                  location->container == containers.hirelingEquipment ||
                                  location->container == containers.beltEquipment);
    if (sourceGrid == grids.end() && !equipped &&
        (!location || location->container != containers.cursor)) {
        drop.error = InventoryError::AccessDenied;
        drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    const auto &definition = *inventory.catalog().find(source->definition);
    auto removeTo = [&](ItemDestination destination) {
        if (location->container == containers.beltEquipment)
            drop.command.emplace(std::in_place_type<EquipBelt>, source->handle(), std::move(destination));
        else if (location->container == containers.hirelingEquipment)
            drop.command = EquipHirelingItem{source->handle(), std::nullopt, std::move(destination)};
        else
            drop.command.emplace(std::in_place_type<EquipItem>, source->handle(), std::nullopt,
                                 std::move(destination));
    };
    if (auto slot = ui.open ? equipmentAt(mouse, session.state().player.weaponSet) : std::nullopt) {
        if (*slot == EquipmentSlot::Belt) {
            if (location->container == containers.beltEquipment) {
                drop.bounds = equipmentBounds(*slot);
                drop.description = "Release to cancel";
                return drop;
            }
            drop.command.emplace(std::in_place_type<EquipBelt>, source->handle());
        } else
            drop.command.emplace(std::in_place_type<EquipItem>, source->handle(), *slot);
        drop.bounds = equipmentBounds(*slot);
        drop.description = "Equip item";
    } else {
        for (const auto &grid : grids) {
            auto cell = grid.cellAt(mouse);
            if (!cell)
                continue;
            Cell origin{cell->x - ui.drag->grab.x, cell->y - ui.drag->grab.y};
            drop.bounds = grid.itemBounds(origin, definition);
            if (equipped) {
                removeTo(ContainerLocation{grid.container, origin});
                drop.description = "Unequip item";
                break;
            }
            auto target = inventory.item(inventory.itemAt(grid.container, *cell));
            if (target && target->id != source->id) {
                auto targetCell = std::get<ContainerLocation>(target->location).cell;
                drop.bounds = grid.itemBounds(targetCell, definition);
                const auto *targetDefinition = inventory.catalog().find(target->definition);
                if (!ui.forceSwap && targetDefinition->bookCapacity &&
                    (targetDefinition->bookScroll == source->definition || target->definition == source->definition)) {
                    drop.command = LoadBook{source->handle(), target->handle()};
                    drop.description = "Add pages to tome";
                } else if (!ui.forceSwap && definition.maxStack > 1 && target->definition == source->definition) {
                    drop.command = MergeStacks{source->handle(), target->handle()};
                    drop.description = "Merge into this stack";
                } else {
                    drop.command = SwapItems{source->handle(), target->handle()};
                    drop.description = "Swap both items";
                    if (sourceGrid != grids.end()) drop.otherBounds =
                        sourceGrid->itemBounds(location->cell, *inventory.catalog().find(target->definition));
                }
            } else {
                drop.command = MoveItem{source->handle(), ContainerLocation{grid.container, origin}};
                drop.description = "Move item";
            }
            break;
        }
    }
    if (equipped && !drop.command) {
        if (!inventorySurface(ui, mouse) && mouse.x >= 0 && mouse.x < W && mouse.y >= 0 &&
                   !hudSurface(mouse)) {
            if (auto ground = session.dropLocation()) {
                removeTo(*ground);
                drop.description = "Drop equipment";
            } else
                drop.error = InventoryError::InvalidLocation;
        } else
            drop.description = "Release to cancel";
        if (drop.command)
            drop.error = session.previewInventory(*drop.command);
        if (drop.error != InventoryError::None)
            drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    if (!drop.command) {
        if (!inventorySurface(ui, mouse) && mouse.x >= 0 && mouse.x < W && mouse.y >= 0 &&
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
