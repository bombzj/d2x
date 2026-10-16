#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SceneView::drawInventoryDrop(const InventoryDrop &drop, Rectangle surface) const {
    const auto &ui = view_.inventory;
    if (characterView_.dead || !ui.drag || !ui.drag->moved || ui.split || ui.goldDialog || view_.gameMenuOpen ||
        (view_.capturesWorldInput(!ui.playerTradeOpen) && !view_.shopOpen) || drop.bounds.width <= 0)
        return;
    auto bounds = GetCollisionRec(drop.bounds, surface);
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? &paletteBlend_ : actPaletteBlends_.at(size_t(act)).get();
    if (!blend) return;
    // D2Client 1.13c RVA 0x8C6B0: green (0,128,0), forbidden red (128,0,0).
    // The actual palette index and resulting cell pixels come from the current Act PL2.
    blend->drawRectangle(bounds, drop.error == InventoryError::None ? Color{0, 128, 0, 255}
                                                                   : Color{128, 0, 0, 255});
}
void SceneView::drawItemArt(const std::string &key, Rectangle bounds) const {
    const auto *icon = assets_.inventoryIcon(key, {}, mapView().palette);
    if (icon && icon->texture.id) {
        DrawTexturePro(
            icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)},
            {bounds.x + icon->x * inventoryScale(), bounds.y + icon->y * inventoryScale(),
             icon->texture.width * inventoryScale(), icon->texture.height * inventoryScale()},
            {0, 0}, 0, WHITE);
    }
}
void SceneView::drawItemBackground(const InventoryItemView &item, Rectangle bounds, bool hovered) const {
    if (!hovered && item.cellTone == InventoryCellTone::Unknown) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? &paletteBlend_ : actPaletteBlends_.at(size_t(act)).get();
    if (!blend) return;
    const Color color = hovered ? Color{0, 128, 0, 255} :
        item.cellTone == InventoryCellTone::Usable ? Color{0, 0, 128, 255} : Color{128, 0, 0, 255};
    blend->drawRectangle(bounds, color);
}
void SceneView::drawItemIcon(const InventoryItemView &item, Rectangle bounds, bool showSockets) const {
    const auto [width, height] = item.iconCells;
    if (width < 1 || height < 1) return;
    const auto [cellWidth, cellHeight] = inventoryView_.itemCellSize;
    if (cellWidth < 1 || cellHeight < 1) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? &paletteBlend_ : actPaletteBlends_.at(size_t(act)).get();
    // Equipment centers the occupied grid, rather than shrinking the DC6 to fit.
    const Vec origin{bounds.x + std::floor((bounds.width / inventoryScale() - width * cellWidth) / 2) * inventoryScale(),
                     bounds.y + std::floor((bounds.height / inventoryScale() - height * cellHeight) / 2) * inventoryScale()};
    auto draw = [&](const std::string &art, std::array<int, 2> transform, uint32_t flags, Vec position, unsigned frame = 0) {
        const auto *icon = assets_.inventoryIcon(art, transform, act, frame);
        if (!icon || !icon->texture.id) return;
        const Rectangle pixels{position.x + icon->x * inventoryScale(), position.y + icon->y * inventoryScale(),
            icon->texture.width * inventoryScale(), icon->texture.height * inventoryScale()};
        if (flags & 0x400000u) {
            if (blend) blend->drawHalf(icon, pixels);
        } else
            DrawTexturePro(icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)}, pixels, {}, 0, WHITE);
    };
    draw(item.artKey, item.colorTransform, item.nativeFlags, origin, item.artFrame);
    if (!showSockets || !item.identified || !item.socketContentsKnown || !(item.nativeFlags & 0x800u) ||
        item.sockets < 1 || item.sockets > 6 || width > 2 || height > 4) return;
    // D2Client 1.13c RVA 95140 / D0A98, coordinates in half-cell units.
    static constexpr int socketPositions[6][8][12]{
        {
            {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 2, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 3, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 0, 0, 0, 0, 0, 0},
            {1, 2, 1, 4, 1, 6, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 1, 2, 3, 0, 0, 0, 0, 0, 0},
            {2, 1, 2, 3, 2, 5, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 4, 2, 6, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 1, 7, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 3, 1, 3, 3, 1, 0, 0, 0, 0},
            {1, 2, 3, 4, 1, 4, 3, 2, 0, 0, 0, 0},
            {2, 1, 2, 3, 2, 5, 2, 7, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 3, 1, 1, 3, 5, 1, 5, 3, 1, 0, 0},
            {2, 4, 1, 2, 3, 6, 1, 6, 3, 2, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 3, 1, 3, 3, 3, 5},
            {1, 2, 1, 4, 1, 6, 3, 2, 3, 4, 3, 6},
        },
        };
    const auto &positions = socketPositions[item.sockets - 1][(width - 1) * 4 + height - 1];
    const auto *hole = assets_.itemSockets.at(size_t(act)).frame(0, 0);
    for (unsigned index = 0; index < item.sockets; ++index) {
        const int x = positions[index * 2], y = positions[index * 2 + 1];
        if (!x || !y) continue; // No native layout for this footprint/socket count.
        const Vec socket{origin.x + ((x - 1) * cellWidth / 2) * inventoryScale(),
                         origin.y + ((y + 1) * cellHeight / 2) * inventoryScale()};
        if (hole && blend)
            blend->drawSocket(hole, {socket.x + (hole->x - 1) * inventoryScale(),
                socket.y + (hole->y - hole->texture.height) * inventoryScale(),
                hole->texture.width * inventoryScale(), hole->texture.height * inventoryScale()});
        if (index < item.socketItems.size()) {
            const auto &child = item.socketItems[index];
            draw(child.artKey, child.colorTransform, child.nativeFlags,
                 {socket.x, socket.y - cellHeight * inventoryScale()});
        }
    }
}
void SceneView::drawItemTooltip(const InventoryItemView &item, Vec anchor,
                                std::optional<unsigned> price, std::string_view priceLabel) const {
    drawItemText(item.tooltip, item.quality, anchor, price, priceLabel);
}
void SceneView::drawInventory(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.open)
        return;
    const auto &inventory = inventoryView_;
    EntityId backpack = inventoryView_.containers.backpack;
    auto panel = inventoryBounds();
    drawPanelFrame(true);
    if (assets_.inventoryPanel.frames.size() >= 8)
        for (int i = 0; i < 4; ++i) {
            const auto &tile = assets_.inventoryPanel.frames[4 + i].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {panel.x + (i % 2) * 256 * inventoryScale(),
                            panel.y + (i / 2) * 256 * inventoryScale(), tile.width * inventoryScale(),
                            tile.height * inventoryScale()},
                           {0, 0}, 0, WHITE);
        }
    auto hoverCell = inventoryCell(mouse);
    EntityId hovered = hoverCell ? inventory.itemAt(backpack, *hoverCell) : EntityId{};
    const auto weaponSet = inventoryView_.weaponSet;
    const bool overShop = view_.shopOpen && CheckCollisionPointRec(rv(mouse), classicSideBounds(false));
    const auto drop = overShop ? InventoryDrop{} :
        inventoryDrop(inventoryView_, inventoryClient_, ui, mouse, view_.hirelingOpen);
    if (inventoryView_.stashLayout.expansion && weaponSet == 1)
        for (int hand = 0; hand < 2; ++hand) {
            const auto &tile = assets_.weaponTabs.frames[size_t(hand)].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (hand ? 247.f : 16.f) * inventoryScale(),
                 panel.y + 23 * inventoryScale(), tile.width * inventoryScale(),
                 tile.height * inventoryScale()}, {0, 0}, 0, WHITE);
        }
    for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index) {
        auto slot = EquipmentSlot(index);
        if (slot == EquipmentSlot::RightHand) slot = weaponHandSlot(false, weaponSet);
        if (slot == EquipmentSlot::LeftHand) slot = weaponHandSlot(true, weaponSet);
        auto bounds = equipmentBounds(slot);
        drawInventoryDrop(drop, bounds);
        auto equipped = inventory.item(inventory.equipped(inventoryView_.containers, slot));
        if (equipped && !ui.hidesItem(*equipped)) {
            const bool over = !ui.drag && CheckCollisionPointRec(rv(mouse), bounds);
            if (over || equipped->cellTone != InventoryCellTone::Usable)
                drawItemBackground(*equipped, bounds, over);
            auto artBounds = bounds;
            // Native body-location adjustments after centering the grid.
            if (slot == EquipmentSlot::Neck || slot == EquipmentSlot::RightRing ||
                slot == EquipmentSlot::LeftRing || slot == EquipmentSlot::Gloves || slot == EquipmentSlot::Belt)
                artBounds.y -= 2 * inventoryScale();
            if (slot == EquipmentSlot::Torso) artBounds.x += 3 * inventoryScale();
            if (slot == EquipmentSlot::Belt || slot == EquipmentSlot::Feet) artBounds.x += 2 * inventoryScale();
            if (slot == EquipmentSlot::Feet) artBounds.y -= 4 * inventoryScale();
            // Original 0x97 handler RVA ABF50 toggles the secondary-set flag;
            // RVA 953CA adds this offset only to its hand slots in expansion.
            if (inventoryView_.stashLayout.expansion && weaponSet &&
                (slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand)) {
                artBounds.x += 4 * inventoryScale();
                artBounds.y -= 4 * inventoryScale();
            }
            drawItemIcon(*equipped, artBounds, over);
        }
        if (CheckCollisionPointRec(rv(mouse), bounds)) {
            if (equipped)
                hovered = equipped->id;
        }
    }
    std::string hint = ui.pending                  ? "Moving item..."
                       : ui.drag && ui.drag->moved ? drop.description
                                                   : "Select an item or drag it to another slot.";
    for (const auto &grid : inventoryGrids(inventoryView_, ui))
        if (grid.container == backpack)
            drawContainerGrid(grid, mouse);
    const auto goldField = inventoryGold();
    if (const auto *coin = assets_.goldCoin.frame(0, 0))
        DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                       {goldField.x + 3 * inventoryScale(), goldField.y + 4 * inventoryScale(),
                        coin->texture.width * inventoryScale(), coin->texture.height * inventoryScale()},
                       {0, 0}, 0, WHITE);
    if (const auto *close = assets_.questClose.frame(0, 10))
        DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                       inventoryClose(), {0, 0}, 0, WHITE);
    painter_.label(std::to_string(inventoryView_.gold),
                   int(goldField.x + 28 * inventoryScale()), int(goldField.y + 4 * inventoryScale()),
                   int(16 * inventoryScale()), WHITE);
    if (ui.identify)
        painter_.label("SELECT AN UNIDENTIFIED ITEM", int(panel.x + 24 * inventoryScale()),
                       int(panel.y + 364 * inventoryScale()), 12, gold);
    if (!ui.split && !ui.drag) {
        if (auto item = inventory.item(hovered); item && !ui.hidesItem(*item))
            drawItemTooltip(*item, {mouse.x + 170, mouse.y},
                inventoryVendorPrice(item->handle()), view_.shopOpen ? (view_.shopRepair ? "COST" : "SELL VALUE") : "");
    } else if (ui.drag && ui.drag->moved && !overShop && !hint.empty()) {
        int width = painter_.measure(hint, 12) + 24;
        frame({panel.x - width - 12, 450, float(width), 30});
        painter_.label(hint, int(panel.x - width), 459, 12,
                       drop.error == InventoryError::None ? gold : Color{242, 137, 114, 255});
    }
    if (ui.split) {
        DrawRectangleRec(panel, {0, 0, 0, 175});
        auto dialog = splitBounds();
        frame(dialog);
        painter_.label("SPLIT STACK", int(dialog.x + 24), int(dialog.y + 19), 18, gold);
        auto source = inventory.item(ui.split->item.id);
        std::string quantity =
            std::to_string(ui.split->quantity) + " / " + (source ? std::to_string(source->quantity) : "0");
        painter_.label(quantity, int(dialog.x + (dialog.width - painter_.measure(quantity, 20)) / 2),
                       int(dialog.y + 66), 20);
        for (int i = 0; i < 2; ++i) {
            auto box = splitAdjust(i);
            frame(box);
            painter_.label(i ? "+" : "-", int(box.x + 14), int(box.y + 8), 16, gold);
            box = splitButton(i);
            frame(box);
            const char *text = i ? "CANCEL" : "SPLIT";
            painter_.label(text, int(box.x + (box.width - painter_.measure(text, 12)) / 2), int(box.y + 9),
                           12, gold);
        }
        painter_.label("Wheel / arrows: quantity   Enter: confirm", int(dialog.x + 23), int(dialog.y + 108),
                       10);
    }
    if (ui.goldDialog) {
        DrawRectangleRec(panel, {0, 0, 0, 175});
        auto dialog = goldDialogBounds();
        frame(dialog);
        const char *title = ui.goldDialog->action == GoldAction::Deposit ? "DEPOSIT GOLD" :
                            ui.goldDialog->action == GoldAction::Withdraw ? "WITHDRAW GOLD" : "DROP GOLD";
        painter_.label(title, int(dialog.x + 22), int(dialog.y + 17), 18, gold);
        painter_.label("Amount (max " + std::to_string(ui.goldDialog->maximum) + ")",
                       int(dialog.x + 22), int(dialog.y + 53), 12, parchment);
        painter_.label(ui.goldDialog->amount + "_", int(dialog.x + 25), int(dialog.y + 76), 20, WHITE);
        for (int index = 0; index < 2; ++index)
            itemButton(goldDialogButton(index), index ? "CANCEL" : "OK", gold);
    }
}
bool SceneView::drawInventoryCursor(Vec mouse) const {
    if (characterView_.dead) return false;
    const auto &ui = view_.inventory;
    if (!ui.drag || view_.gameMenuOpen ||
        (view_.capturesWorldInput(!ui.playerTradeOpen) && !view_.shopOpen))
        return false;
    const auto *item = inventoryView_.item(ui.drag->item.id);
    if (!item || item->revision != ui.drag->item.revision)
        return false;
    const auto &definition = *inventoryView_.definition(item->definition);
    const auto *icon = assets_.inventoryIcon(item->artKey, item->colorTransform, mapView().palette, item->artFrame);
    if (!icon) return true;
    // Native cursor RVA 169B8 centers the DC6 dimensions, not its grid footprint.
    drawItemIcon(*item,
                 {mouse.x - (icon->texture.width / 2) * inventoryScale(),
                  mouse.y - (icon->texture.height / 2) * inventoryScale(),
                  definition.width * inventoryCellSize(), definition.height * inventoryCellSize()});
    return true;
}
} // namespace d2x
