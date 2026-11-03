#include "inventory_panel.hpp"
#include "presentation/npc/hireling_panel.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
Rectangle equipmentBounds(const InventoryView &inventory, EquipmentSlot slot) {
    if (slot == EquipmentSlot::AlternateRightHand) slot = EquipmentSlot::RightHand;
    if (slot == EquipmentSlot::AlternateLeftHand) slot = EquipmentSlot::LeftHand;
    if (size_t(slot) >= inventory.equipmentSlots.size())
        return {};
    const auto &box = inventory.equipmentSlots[size_t(slot)];
    auto panel = inventoryBounds(inventory);
    return {panel.x + box[0] * inventoryScale(), panel.y + box[1] * inventoryScale(),
            (box[2] - box[0]) * inventoryScale(), (box[3] - box[1]) * inventoryScale()};
}
std::optional<EquipmentSlot> equipmentAt(const InventoryView &inventory, Vec mouse, unsigned weaponSet) {
    for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index)
        if (CheckCollisionPointRec(rv(mouse), equipmentBounds(inventory, EquipmentSlot(index)))) {
            auto slot = EquipmentSlot(index);
            return slot == EquipmentSlot::RightHand ? weaponHandSlot(false, weaponSet) :
                   slot == EquipmentSlot::LeftHand ? weaponHandSlot(true, weaponSet) : slot;
        }
    return std::nullopt;
}
Rectangle weaponTabBounds(unsigned set, bool left) {
    auto panel = inventoryBounds();
    return {panel.x + (left ? 248.f : 17.f) * inventoryScale() + set * 32.f * inventoryScale(),
            panel.y + 23.f * inventoryScale(), 30.f * inventoryScale(), 20.f * inventoryScale()};
}
std::optional<unsigned> weaponTabAt(Vec mouse) {
    for (unsigned set = 0; set < 2; ++set)
        if (CheckCollisionPointRec(rv(mouse), weaponTabBounds(set, false)) ||
            CheckCollisionPointRec(rv(mouse), weaponTabBounds(set, true)))
            return set;
    return std::nullopt;
}
std::optional<Cell> inventoryCell(const InventoryView &inventory, Vec mouse) {
    auto grid = inventoryGrid(inventory);
    if (inventoryCellSize(inventory) <= 0 || !CheckCollisionPointRec(rv(mouse), grid))
        return std::nullopt;
    return Cell{int((mouse.x - grid.x) / inventoryCellSize(inventory)), int((mouse.y - grid.y) / inventoryCellSize(inventory))};
}
std::vector<ContainerGrid> inventoryGrids(const InventoryView &inventory, const InventoryUi &ui) {
    const auto &c = inventory.containers;
    std::vector<ContainerGrid> grids;
    if (const auto *container = inventory.container(c.belt)) {
        const int rows = ui.visibleBeltRows(container->rows);
        const auto belt = beltSlot({0, 0});
        grids.push_back({c.belt, {belt.x, belt.y}, {31 * hudScale(), -32 * hudScale()},
                         container->columns, rows, 29 * hudScale()});
    }
    if (ui.playerTradeOpen) grids.clear();
    if (ui.open && inventory.container(c.backpack) && inventory.backpackLayout.cellSize > 0) {
        auto p = inventoryGrid(inventory);
        grids.push_back(
            {c.backpack, {p.x, p.y}, {inventoryCellSize(inventory), inventoryCellSize(inventory)},
             inventory.backpackLayout.columns, inventory.backpackLayout.rows, inventoryCellSize(inventory)});
    }
    if (ui.storage && inventory.storage == ui.storage && !inventory.storageMaterialsOpen) {
        auto p = storageBounds(inventory);
        const auto &spec = *inventory.container(ui.storage);
        const auto &layout = inventory.stashLayout;
        grids.push_back({ui.storage,
                         {p.x + layout.left * inventoryScale(), p.y + layout.top * inventoryScale()},
                         {layout.cellSize * inventoryScale(), layout.cellSize * inventoryScale()},
                         spec.columns,
                         spec.rows,
                         layout.cellSize * inventoryScale()});
    }
    if (ui.cubeOpen && c.cube) {
        auto p = cubeBounds();
        const auto &layout = inventory.cubeLayout;
        grids.push_back({c.cube,
                         {p.x + layout.left * inventoryScale(), p.y + layout.top * inventoryScale()},
                         {layout.cellSize * inventoryScale(), layout.cellSize * inventoryScale()},
                         layout.columns, layout.rows, layout.cellSize * inventoryScale()});
    }
    if (ui.playerTradeOpen && inventory.ownTrade) grids.push_back(playerTradeGrid(inventory,true));
    return grids;
}
ContainerGrid playerTradeGrid(const InventoryView &inventory, bool own) {
    const auto p = classicPanelBounds(false);
    const auto &l = own ? inventory.ownTradeLayout : inventory.peerTradeLayout;
    const float cell = l.cellSize*inventoryScale();
    return {own ? inventory.ownTrade : inventory.peerTrade,
        {p.x+l.left*inventoryScale(),p.y+l.top*inventoryScale()},{cell,cell},l.columns,l.rows,cell};
}
void InventoryUi::syncCursor(const InventoryView &inventory, EntityId reserved) {
    if (!pending) {
        if (pendingPlacement && pendingPlacement->taking && drag && !drag->onCursor &&
            drag->item.id == pendingPlacement->item && !inventory.cursorItem()) drag.reset();
        pendingPlacement.reset();
    }
    if (const auto *item = inventory.cursorItem()) {
        // Suppress a newly assigned intermediate cursor during a queued take,
        // but keep an already authoritative cursor until placement is confirmed.
        if (item->id == reserved || (pendingPlacement && !pendingPlacement->taking && pending==item->id && pendingPlacement->item==item->id &&
            pendingPlacement->source!=item->location)) {
            drag.reset();
            return;
        }
        if (!drag || drag->item.id != item->id) {
            drag = InventoryDrag{item->handle(), {}, true, true, true};
        } else { drag->item = item->handle(); drag->onCursor = true; }
        identify.reset();
    } else if (drag && drag->onCursor) drag.reset();
}
bool inventorySurface(const InventoryUi &ui, Vec mouse) {
    return (ui.open && CheckCollisionPointRec(rv(mouse), classicSideBounds(true))) ||
           ((ui.storage || ui.cubeOpen || ui.playerTradeOpen) && CheckCollisionPointRec(rv(mouse), classicSideBounds(false)));
}
InventoryDrop inventoryDrop(const InventoryView &inventory, const IInventoryClient &client, const InventoryUi &ui, Vec mouse,
                            std::array<int, 2> cursorPixels, bool hirelingOpen) {
    InventoryDrop drop;
    if (!ui.drag)
        return drop;
    const auto *source = inventory.item(ui.drag->item.id);
    if (!source || source->revision != ui.drag->item.revision) {
        drop.error = source ? InventoryError::SourceChanged : InventoryError::UnknownItem;
        drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    auto grids = inventoryGrids(inventory, ui);
    auto location = std::get_if<ContainerLocation>(&source->location);
    auto sourceGrid = std::find_if(grids.begin(), grids.end(), [&](const auto &grid) {
        return location && grid.container == location->container;
    });
    const auto &containers = inventory.containers;
    if (hirelingOpen && CheckCollisionPointRec(rv(mouse), classicSideBounds(false))) {
        const auto &order = hirelingEquipmentOrder;
        const auto &slots = inventory.hirelingSlots;
        drop.description = "Release to cancel";
        for (size_t index = 0; index < slots.size(); ++index) {
            const auto &box = slots[index];
            if (box[0] < 0) continue;
            auto bounds = hirelingArtRect(float(box[0]), float(box[1]),
                                           float(box[2] - box[0]), float(box[3] - box[1]));
            if (!CheckCollisionPointRec(rv(mouse), bounds)) continue;
            drop.bounds = bounds;
            drop.command = EquipHirelingItem{source->handle(), order[index], {}};
            drop.error = client.preview(*drop.command);
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
    const auto &definition = *inventory.definition(source->definition);
    // Retail 1.13c RVA 8C2B0 only selects 0x28 for an identified,
    // unbroken socketed host with room. Other targets keep normal swap/equip.
    const auto socketTarget = [&](const InventoryItemView &host) {
        return !ui.playerTradeOpen && definition.socketFiller && source->identified &&
            host.identified && (host.nativeFlags & 0x800u) && !(host.nativeFlags & 0x100u) &&
            host.socketContentsKnown && host.socketItems.size() < host.sockets;
    };
    auto removeTo = [&](ItemDestination destination) {
        if (location->container == containers.beltEquipment)
            drop.command.emplace(std::in_place_type<EquipBelt>, source->handle(), std::move(destination));
        else if (location->container == containers.hirelingEquipment)
            drop.command = EquipHirelingItem{source->handle(), std::nullopt, std::move(destination)};
        else
            drop.command.emplace(std::in_place_type<EquipItem>, source->handle(), std::nullopt,
                                 std::move(destination));
    };
    if (auto slot = ui.open && !ui.playerTradeOpen ? equipmentAt(inventory, mouse, inventory.weaponSet) : std::nullopt) {
        const auto *host = inventory.item(inventory.equipped(containers, *slot));
        if (host && socketTarget(*host)) {
            drop.command = SocketItem{source->handle(), host->handle()};
            drop.description = "Insert into socket";
        } else if (*slot == EquipmentSlot::Belt) {
            if (location->container == containers.beltEquipment) {
                drop.bounds = equipmentBounds(inventory, *slot);
                drop.description = "Release to cancel";
                return drop;
            }
            drop.command.emplace(std::in_place_type<EquipBelt>, source->handle());
        } else
            drop.command.emplace(std::in_place_type<EquipItem>, source->handle(), *slot);
        drop.bounds = equipmentBounds(inventory, *slot);
        if (!std::holds_alternative<SocketItem>(*drop.command)) drop.description = "Equip item";
    } else {
        for (const auto &grid : grids) {
            auto cell = grid.cellAt(mouse);
            if (!cell)
                continue;
            if (grid.container == containers.belt && (!definition.beltAllowed || definition.width != 1 || definition.height != 1)) {
                drop.bounds = beltSlot(*cell);
                drop.error = InventoryError::UnsupportedUse;
                drop.description = inventoryErrorText(drop.error);
                return drop;
            }
            const auto *pointed = inventory.item(inventory.itemAt(grid.container,*cell));
            const auto *pointedDefinition = pointed ? inventory.definition(pointed->definition) : nullptr;
            if (!ui.playerTradeOpen && pointed && pointed->id != source->id && pointedDefinition &&
                pointedDefinition->opensCube && grid.container != containers.cube) {
                drop.bounds = grid.itemBounds(std::get<ContainerLocation>(pointed->location).cell,*pointedDefinition);
                drop.command = MoveItem{source->handle(),AutoPlace{containers.cube}};
                drop.error = client.preview(*drop.command);
                return drop;
            }
            // D2Client 1.13c RVA 945D0/94820: even footprints shift the
            // pointer by a quarter of the DC6 dimension before cell division.
            // Both native paths then subtract half the footprint and clamp at 0.
            auto axis = [](int cell, float pointer, float start, float stride,
                           int extent, int capacity, int pixels) {
                if (extent % 2 == 0)
                    cell = int(std::floor((pointer - start + (pixels / 4) * inventoryScale()) / stride));
                if (extent == capacity) cell = capacity / 2;
                return std::max(0, cell - extent / 2);
            };
            Cell origin{
                axis(cell->x, mouse.x, grid.origin.x, grid.stride.x,
                     definition.width, grid.columns, cursorPixels[0]),
                axis(cell->y, mouse.y, grid.origin.y, grid.stride.y,
                     definition.height, grid.rows, cursorPixels[1])};
            drop.bounds = grid.itemBounds(origin, definition);
            if (origin.x < 0 || origin.y < 0 || origin.x + definition.width > grid.columns ||
                origin.y + definition.height > grid.rows) {
                drop.error = InventoryError::OutOfBounds;
                drop.description = inventoryErrorText(drop.error);
                return drop;
            }
            // Count distinct items over the entire held footprint, not only the
            // cell beneath the pointer. Multiple cells of one item count once.
            EntityId targetId;
            for (int y = origin.y; y < origin.y + definition.height; ++y)
                for (int x = origin.x; x < origin.x + definition.width; ++x) {
                    const auto id = inventory.itemAt(grid.container, {x, y});
                    if (!id || id == source->id) continue;
                    if (targetId && targetId != id) {
                        drop.error = InventoryError::Occupied;
                        drop.description = inventoryErrorText(drop.error);
                        return drop;
                    }
                    targetId = id;
                }
            auto target = inventory.item(targetId);
            if (target) {
                auto targetCell = std::get<ContainerLocation>(target->location).cell;
                const auto *targetDefinition = inventory.definition(target->definition);
                drop.bounds = grid.itemBounds(targetCell, *targetDefinition);
                if (socketTarget(*target)) {
                    drop.command = SocketItem{source->handle(), target->handle()};
                    drop.description = "Insert into socket";
                } else if (!ui.playerTradeOpen && targetDefinition->bookCapacity &&
                    targetDefinition->bookScroll == source->definition) {
                    drop.command = LoadBook{source->handle(), target->handle()};
                    drop.description = "Add pages to tome";
                } else if (!ui.playerTradeOpen && definition.maxStack > 1 && target->definition == source->definition &&
                    source->quality == target->quality && (source->quality == ItemQuality::Normal ||
                    source->quality == ItemQuality::Superior || source->quality == ItemQuality::Inferior) &&
                    !((source->nativeFlags ^ target->nativeFlags) & 0x00400000u) &&
                    !source->sockets && !target->sockets && source->stack && target->stack && source->stack == target->stack) {
                    drop.command = MergeStacks{source->handle(), target->handle()};
                    drop.description = "Merge into this stack";
                } else {
                    drop.command = SwapItems{source->handle(), target->handle(),
                                             ContainerLocation{grid.container, origin}};
                    drop.description = "Place held item and pick up covered item";
                }
            } else if (equipped) {
                removeTo(ContainerLocation{grid.container, origin});
                drop.description = "Unequip item";
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
            if (auto ground = inventory.dropLocation) {
                removeTo(*ground);
                drop.description = "Drop equipment";
            } else
                drop.error = InventoryError::InvalidLocation;
        } else
            drop.description = "Release to cancel";
        if (drop.command)
            drop.error = client.preview(*drop.command);
        if (drop.error != InventoryError::None)
            drop.description = inventoryErrorText(drop.error);
        return drop;
    }
    if (!drop.command) {
        if (!inventorySurface(ui, mouse) && mouse.x >= 0 && mouse.x < W && mouse.y >= 0 &&
                   !hudSurface(mouse)) {
            if (auto ground = inventory.dropLocation) {
                drop.command = MoveItem{source->handle(), *ground};
                drop.description = "Drop at your feet";
            } else
                drop.error = InventoryError::InvalidLocation;
        } else
            drop.description = "Release to cancel";
    }
    if (drop.command)
        drop.error = client.preview(*drop.command);
    if (drop.error != InventoryError::None)
        drop.description = inventoryErrorText(drop.error);
    return drop;
}

float inventoryScale() { return classicPanelScale(); }

float inventoryCellSize(const InventoryView &inventory) { return inventory.itemCellSize[0] * inventoryScale(); }

Rectangle orificeBounds() {return {106*inventoryScale(),98*inventoryScale(),108*inventoryScale(),164*inventoryScale()};}

Rectangle orificeSlot() {const auto p=orificeBounds();return {p.x+16*inventoryScale(),p.y+12*inventoryScale(),76*inventoryScale(),108*inventoryScale()};}

Rectangle orificeButton(bool confirm) {const auto p=orificeBounds();return {p.x+(confirm?10:66)*inventoryScale(),p.y+126*inventoryScale(),32*inventoryScale(),32*inventoryScale()};}

Rectangle inventoryBounds() {
    return classicPanelBounds(true);
}

Rectangle inventoryBounds(const InventoryView &inventory) {
    auto panel = classicPanelBounds(true, float(inventory.panelTop));
    panel.height = inventory.panelHeight * inventoryScale();
    return panel;
}

Rectangle inventoryGrid(const InventoryView &inventory) {
    auto p = inventoryBounds(inventory);
    const auto &layout = inventory.backpackLayout;
    return {p.x + layout.left * inventoryScale(), p.y + layout.top * inventoryScale(),
            layout.columns * inventoryCellSize(inventory), layout.rows * inventoryCellSize(inventory)};
}

Rectangle inventoryClose() {
    auto p = inventoryBounds();
    // The 32x32 buysellbtn frame fits the invchar6 socket at (18,385).
    return {p.x + 18 * inventoryScale(), p.y + 385 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle inventoryGold() {
    auto p = inventoryBounds();
    return {p.x + 82 * inventoryScale(), p.y + 391 * inventoryScale(),
            120 * inventoryScale(), 20 * inventoryScale()};
}

Rectangle inventoryGoldButton() {
    const auto p = inventoryBounds();
    // Native 1.13c goldcoinbtn anchor, inside the invchar6 gold socket.
    return {p.x + 84 * inventoryScale(), p.y + 392 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}

Rectangle inventoryClose(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryClose();
    const auto p = inventoryBounds(inventory);
    return {p.x + 18 * inventoryScale(), p.y + (inventory.panelHeight - 4) * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle inventoryGold(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryGold();
    const auto p = inventoryBounds(inventory);
    // CN invchar6 frames 4-7: the gold socket starts at (72,468),
    // with the extended art's signed DC6 origin at Y=25.
    return {p.x + 72 * inventoryScale(), p.y + (inventory.panelHeight + 1) * inventoryScale(),
            116 * inventoryScale(), 20 * inventoryScale()};
}

Rectangle inventoryGoldButton(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryGoldButton();
    const auto field = inventoryGold(inventory);
    return {field.x + 2 * inventoryScale(), field.y + 2 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}

Rectangle inventoryToggle() {
    return hudMenuButton();
}

Rectangle beltSlot(Cell cell) {
    return hudBeltSlot(cell.x, cell.y);
}

Rectangle beltBounds(int rows) {
    return hudRect(424, 42 + (rows - 1) * 32, 125, rows * 32);
}

std::optional<Cell> beltCell(Vec mouse, int rows) {
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < 4; ++x)
            if (CheckCollisionPointRec(rv(mouse), beltSlot({x, y})))
                return Cell{x, y};
    return std::nullopt;
}

Rectangle storageBounds() {
    return classicPanelBounds(false);
}

Rectangle storageBounds(const InventoryView &inventory) {
    auto panel = classicPanelBounds(false, float(inventory.storagePanelTop));
    panel.height = inventory.storagePanelHeight * inventoryScale();
    return panel;
}

Rectangle storageNavigationButton(unsigned button, const InventoryView &inventory) {
    const auto p = classicPanelBounds(false);
    constexpr std::array x{30, 75, 120, 165, 210, 15};
    const auto grid = storageBounds(inventory);
    const float y = button == 5 ? grid.y + (inventory.storagePanelHeight - 14) * inventoryScale() :
        grid.y + (inventory.stashLayout.top - 34) * inventoryScale();
    return {p.x + x.at(button) * inventoryScale(), y,
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle storageMaterialBounds(const InventoryMaterialView &item) {
    const auto p = classicPanelBounds(false);
    const auto scale = inventoryScale();
    // 0x275E0/0x27600 and 0x27B80/0x27BA0; native DC6 baseline to top.
    return {p.x + (item.x + 2) * scale,
            p.y + (item.y - 1 - item.height * 29) * scale,
            item.width * 29 * scale, item.height * 29 * scale};
}

Rectangle storageMaterialUpgradeBounds(const InventoryMaterialView &item) {
    auto bounds = storageMaterialBounds(item);
    bounds.x += (item.upgradeOnLeft ? 0 : 24) * inventoryScale();
    bounds.width = 5 * inventoryScale();
    return bounds;
}

Rectangle storageClose() {
    return {storageBounds().x + 274 * inventoryScale(), storageBounds().y + 384 * inventoryScale(),
            31 * inventoryScale(), 34 * inventoryScale()};
}

Rectangle storageClose(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageClose();
    const auto p = storageBounds(inventory);
    return {p.x + 274 * inventoryScale(), p.y + (inventory.storagePanelHeight - 14) * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle cubeBounds() {
    return classicPanelBounds(false);
}

Rectangle cubeClose(const InventoryView &inventory) {
    auto p = cubeBounds();
    return {p.x + inventory.cubeCloseOffset[0] * inventoryScale(), p.y + inventory.cubeCloseOffset[1] * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle cubeTransmute() {
    auto p = cubeBounds();
    // D2Client RVA 99657/99678: miniconvert at X=144, baseline=292.
    return {p.x + 144 * inventoryScale(), p.y + 260 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle storageGold(bool expansion) {
    auto p = storageBounds();
    return {p.x + 74 * inventoryScale(),
            p.y + (expansion ? 20 : 218) * inventoryScale(),
            176 * inventoryScale(), 22 * inventoryScale()};
}

Rectangle storageGoldButton(bool expansion) {
    const auto p = storageBounds();
    // Native stash button moves down 198 pixels in the classic layout.
    return {p.x + 75 * inventoryScale(),
            p.y + (expansion ? 23 : 221) * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}

Rectangle storageGold(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageGold(inventory.stashLayout.expansion);
    const auto p = storageBounds(inventory);
    // CN tradestash's two bottom fields are bank gold and the page name.
    return {p.x + 72 * inventoryScale(), p.y + (inventory.storagePanelHeight - 18) * inventoryScale(),
            176 * inventoryScale(), 20 * inventoryScale()};
}

Rectangle storageGoldButton(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageGoldButton(inventory.stashLayout.expansion);
    const auto field = storageGold(inventory);
    return {field.x + 2 * inventoryScale(), field.y + 2 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}

Rectangle storageNameField(const InventoryView &inventory) {
    auto bounds = storageGold(inventory);
    bounds.y += 21 * inventoryScale();
    bounds.height = 20 * inventoryScale();
    return bounds;
}

Rectangle goldDialogBounds() {
    // Retail gold menu starts at (215,140) in 640x480, centered when the
    // game canvas grows. The MPQ dialogbackground frame is 210x158.
    return {W / 2.f - 105 * inventoryScale(), (H - HUD) / 2.f - 79 * inventoryScale(),
            210 * inventoryScale(), 158 * inventoryScale()};
}

Rectangle goldDialogButton(int index) {
    auto p = goldDialogBounds();
    return {p.x + (35 + index * 105) * inventoryScale(), p.y + 115 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}

Rectangle goldDialogEntry() {
    const auto p = goldDialogBounds();
    return {p.x + 29 * inventoryScale(), p.y + 62 * inventoryScale(),
            152 * inventoryScale(), 24 * inventoryScale()};
}
} // namespace d2x
