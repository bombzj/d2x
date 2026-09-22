#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::drawItemIcon(const ItemInstance &item, Rectangle bounds, Color tint) const {
    auto found = assets_.itemIcons.find(item.definition);
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
        painter_.label(item.definition, int(bounds.x + 4), int(bounds.y + 4), 10, tint);
    }
}
void SceneView::drawInventory(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.open)
        return;
    const auto &inventory = session_.inventory();
    EntityId backpack = session_.playerContainers().backpack;
    auto panel = inventoryBounds();
    DrawRectangle(int(panel.x) - 6, 0, int(panel.width) + 22, H - HUD, {0, 0, 0, 150});
    frame(panel);
    if (assets_.inventoryPanel.frames.size() >= 8)
        for (int i = 0; i < 4; ++i) {
            const auto &tile = assets_.inventoryPanel.frames[4 + i].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {panel.x + (i % 2) * 256 * inventoryScale,
                            panel.y + (i / 2) * 256 * inventoryScale, tile.width * inventoryScale,
                            tile.height * inventoryScale},
                           {0, 0}, 0, WHITE);
        }
    frame(inventoryClose(), CheckCollisionPointRec(rv(mouse), inventoryClose()) ? parchment : gold);
    painter_.label("X", int(inventoryClose().x + 7), int(inventoryClose().y + 6), 12, gold);
    auto hoverCell = inventoryCell(mouse);
    EntityId hovered = hoverCell ? inventory.itemAt(backpack, *hoverCell) : EntityId{};
    for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
        auto slot = EquipmentSlot(index);
        auto bounds = equipmentBounds(slot);
        auto equipped = inventory.item(inventory.equipped(session_.playerContainers(), slot));
        if (equipped)
            drawItemIcon(*equipped, bounds,
                         ui.drag && ui.drag->moved && ui.drag->item.id == equipped->id ? Fade(WHITE, .3f) : WHITE);
        if (CheckCollisionPointRec(rv(mouse), bounds)) {
            DrawRectangleLinesEx(bounds, 1, parchment);
            if (equipped)
                hovered = equipped->id;
        }
    }
    auto drop = inventoryDrop(session_, ui, mouse);
    std::string hint = ui.pending                  ? "Moving item..."
                       : ui.drag && ui.drag->moved ? drop.description
                                                   : "Select an item or drag it to another slot.";
    for (const auto &grid : inventoryGrids(session_, ui))
        if (grid.container == backpack)
            drawContainerGrid(grid, mouse);
    if (ui.drag && ui.drag->moved && drop.bounds.width > 0) {
        Color color = drop.error == InventoryError::None ? Color{99, 202, 118, 255} : Color{240, 91, 68, 255};

        DrawRectangleRec(drop.bounds, Fade(color, .2f));
        DrawRectangleLinesEx(drop.bounds, 2, color);
        if (drop.otherBounds)
            DrawRectangleLinesEx(*drop.otherBounds, 2, color);
    }
    const char *buttons[] = {"AUTO PLACE", "SPLIT", "DROP"};
    const auto *selected = inventory.item(ui.selected);
    for (int i = 0; i < 3; ++i) {
        auto box = inventoryButton(i);
        bool enabled = selected && !ui.pending && (i != 1 || selected->quantity > 1);
        Color color =
            enabled ? CheckCollisionPointRec(rv(mouse), box) ? parchment : gold : Color{85, 83, 71, 255};
        itemButton(box, buttons[i], color);
    }
    const auto &stats = session_.equipmentStats();
    std::string damageText = "Damage ";
    for (int index = 0; index < stats.weaponCount; ++index) {
        if (index)
            damageText += " / ";
        const auto &weapon = stats.weapons[index];
        damageText += std::to_string(weapon.minimum / 256) + "-" + std::to_string(weapon.maximum / 256);
    }
    painter_.label(damageText, int(panel.x + 20), 539, 10, parchment);
    auto armorText = "Defense " + std::to_string(stats.defense) + "  Block " +
                     std::to_string(stats.blockChance) + "%";
    painter_.label(armorText, int(panel.x + 195), 539, 10, parchment);
    if (!ui.split && !ui.drag) {
        if (auto item = inventory.item(hovered))
            drawItemTooltip(*item, {panel.x - 12, mouse.y});
    } else if (ui.drag && ui.drag->moved && !hint.empty()) {
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
}
void SceneView::drawInventoryCursor(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.drag || !ui.drag->moved || view_.blocksWorld())
        return;
    const auto *item = session_.inventory().item(ui.drag->item.id);
    if (!item || item->revision != ui.drag->item.revision)
        return;
    const auto &definition = *session_.inventory().catalog().find(item->definition);
    drawItemIcon(*item,
                 {mouse.x - ui.drag->pixelOffset.x, mouse.y - ui.drag->pixelOffset.y,
                  definition.width * inventoryCellSize, definition.height * inventoryCellSize},
                 Fade(WHITE, .85f));
}
} // namespace d2x
