#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::drawItemArt(const std::string &key, const std::string &code, Rectangle bounds, Color tint) const {
    auto found = assets_.itemIcons.find(key);
    auto icon = found == assets_.itemIcons.end() ? nullptr : found->second.frame(0, 0);
    if (icon && icon->texture.id) {
        float scale = std::min({inventoryScale, (bounds.width - 4) / icon->texture.width,
                                (bounds.height - 4) / icon->texture.height});
        float width = icon->texture.width * scale, height = icon->texture.height * scale;
        DrawTexturePro(
            icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)},
            {bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2, width, height},
            {0, 0}, 0, tint);
    } else {
        diamond({bounds.x + bounds.width / 2, bounds.y + bounds.height / 2}, 9, gold);
        painter_.label(code, int(bounds.x + 4), int(bounds.y + 4), 10, tint);
    }
}
void SceneView::drawItemIcon(const InventoryItemView &item, Rectangle bounds, Color tint) const {
    if (item.nativeFlags & 0x00400000u) tint.a = uint8_t((unsigned(tint.a) + 1) / 2);
    drawItemArt(item.artKey, item.definition, bounds, tint);
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
                           {panel.x + (i % 2) * 256 * inventoryScale,
                            panel.y + (i / 2) * 256 * inventoryScale, tile.width * inventoryScale,
                            tile.height * inventoryScale},
                           {0, 0}, 0, WHITE);
        }
    auto hoverCell = inventoryCell(mouse);
    EntityId hovered = hoverCell ? inventory.itemAt(backpack, *hoverCell) : EntityId{};
    const auto weaponSet = inventoryView_.weaponSet;
    if (inventoryView_.stashLayout.expansion && weaponSet == 1)
        for (int hand = 0; hand < 2; ++hand) {
            const auto &tile = assets_.weaponTabs.frames[size_t(hand)].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (hand ? 247.f : 16.f) * inventoryScale,
                 panel.y + 23 * inventoryScale, tile.width * inventoryScale,
                 tile.height * inventoryScale}, {0, 0}, 0, WHITE);
        }
    for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index) {
        auto slot = EquipmentSlot(index);
        if (slot == EquipmentSlot::RightHand) slot = weaponHandSlot(false, weaponSet);
        if (slot == EquipmentSlot::LeftHand) slot = weaponHandSlot(true, weaponSet);
        auto bounds = equipmentBounds(slot);
        auto equipped = inventory.item(inventory.equipped(inventoryView_.containers, slot));
        if (equipped && !(ui.drag && ui.drag->item.id == equipped->id)) {
            drawItemIcon(*equipped, bounds);
            if (inventory.definition(equipped->definition)->maxStack > 1) {
                auto quantity = std::to_string(equipped->quantity);
                painter_.label(quantity, int(bounds.x + bounds.width - painter_.measure(quantity, 12) - 4),
                               int(bounds.y + bounds.height - 16), 12, equipped->quantity ? WHITE : RED);
            }
        }
        if (CheckCollisionPointRec(rv(mouse), bounds)) {
            DrawRectangleLinesEx(bounds, 1, parchment);
            if (equipped)
                hovered = equipped->id;
        }
    }
    const bool overShop = view_.shopOpen && CheckCollisionPointRec(rv(mouse), classicSideBounds(false));
    auto drop = overShop ? InventoryDrop{} : inventoryDrop(inventoryView_, inventoryClient_, ui, mouse, view_.hirelingOpen);
    std::string hint = ui.pending                  ? "Moving item..."
                       : ui.drag && ui.drag->moved ? drop.description
                                                   : "Select an item or drag it to another slot.";
    for (const auto &grid : inventoryGrids(inventoryView_, ui))
        if (grid.container == backpack)
            drawContainerGrid(grid, mouse);
    if (ui.drag && ui.drag->moved && drop.bounds.width > 0) {
        Color color = drop.error == InventoryError::None ? Color{99, 202, 118, 255} : Color{240, 91, 68, 255};

        DrawRectangleRec(drop.bounds, Fade(color, .2f));
        DrawRectangleLinesEx(drop.bounds, 2, color);
        if (drop.otherBounds)
            DrawRectangleLinesEx(*drop.otherBounds, 2, color);
    }
    const auto goldField = inventoryGold();
    if (const auto *coin = assets_.goldCoin.frame(0, 0))
        DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                       {goldField.x + 3 * inventoryScale, goldField.y + 4 * inventoryScale,
                        coin->texture.width * inventoryScale, coin->texture.height * inventoryScale},
                       {0, 0}, 0, WHITE);
    if (const auto *close = assets_.questClose.frame(0, 10))
        DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                       inventoryClose(), {0, 0}, 0, WHITE);
    painter_.label(std::to_string(inventoryView_.gold),
                   int(goldField.x + 28 * inventoryScale), int(goldField.y + 4 * inventoryScale),
                   int(16 * inventoryScale), WHITE);
    if (ui.identify)
        painter_.label("SELECT AN UNIDENTIFIED ITEM", int(panel.x + 24 * inventoryScale),
                       int(panel.y + 364 * inventoryScale), 12, gold);
    if (!ui.split && !ui.drag) {
        if (auto item = inventory.item(hovered))
            drawItemTooltip(*item, {panel.x - 12, mouse.y},
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
        (view_.capturesWorldInput() && !view_.shopOpen))
        return false;
    const auto *item = inventoryView_.item(ui.drag->item.id);
    if (!item || item->revision != ui.drag->item.revision)
        return false;
    const auto &definition = *inventoryView_.definition(item->definition);
    drawItemIcon(*item,
                 {mouse.x - ui.drag->pixelOffset.x, mouse.y - ui.drag->pixelOffset.y,
                  definition.width * inventoryCellSize, definition.height * inventoryCellSize});
    return true;
}
} // namespace d2x
