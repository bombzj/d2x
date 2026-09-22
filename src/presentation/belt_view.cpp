#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::itemButton(Rectangle bounds, const char *label, Color color) const {
    if (auto sprite = assets_.button.frame(0, 0)) {
        auto texture = sprite->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds, {0, 0}, 0,
                       WHITE);
    } else
        frame(bounds, color);
    painter_.label(label, int(bounds.x + (bounds.width - painter_.measure(label, 12)) / 2),
                   int(bounds.y + (bounds.height - 12) / 2), 12, color);
}
void SceneView::drawItemTooltip(const ItemInstance &item, Vec anchor) const {
    const auto &definition = *session_.inventory().catalog().find(item.definition);
    std::vector<std::string> lines{definition.name};
    const char *qualities[] = {"Normal", "Magic", "Rare", "Set", "Unique"};
    auto quality = size_t(item.quality);
    lines.push_back(std::string(quality < std::size(qualities) ? qualities[quality] : "Unknown") +
                    " / Item level " + std::to_string(item.level));
    if (auto potion = potionDefinition(item.definition)) {
        switch (potion->kind) {
        case PotionKind::Healing:
            lines.push_back("Life: +" + std::to_string(int(potion->amount)) + " over 8 seconds");
            break;
        case PotionKind::Mana:
            lines.push_back("Mana: +" + std::to_string(int(potion->amount)) + " over 5.12 seconds");
            break;
        case PotionKind::Rejuvenation:
            lines.push_back("Instant life and mana: " + std::to_string(int(potion->amount * 100)) + "%");
            break;
        case PotionKind::Stamina:
            lines.push_back("Full stamina; no drain for 30 seconds");
            break;
        }
        lines.push_back("Right-click to drink");
    } else if (definition.beltRows) {
        lines.push_back("Belt capacity: " + std::to_string(4 * definition.beltRows) + " potions / scrolls");
        lines.push_back("Right-click to equip / remove");
    } else {
        lines.push_back("Size: " + std::to_string(definition.width) + " x " +
                        std::to_string(definition.height));
        if (definition.usable)
            lines.push_back("Use effect not implemented yet");
    }
    if (item.quantity > 1)
        lines.push_back("Quantity: " + std::to_string(item.quantity) + " / " +
                        std::to_string(definition.maxStack));
    if (definition.maxDurability)
        lines.push_back("Durability: " + std::to_string(item.durability) + " / " +
                        std::to_string(definition.maxDurability));
    const auto &base = definition.base;
    if (definition.family == ItemFamily::Weapon && base.minDamage && base.maxDamage)
        lines.push_back("Base damage: " + std::to_string(*base.minDamage) + " - " +
                        std::to_string(*base.maxDamage));
    if (definition.family == ItemFamily::Armor && base.minDefense && base.maxDefense)
        lines.push_back("Base defense range: " + std::to_string(*base.minDefense) + " - " +
                        std::to_string(*base.maxDefense));
    if (base.requiredStrength && *base.requiredStrength > 0)
        lines.push_back("Required strength: " + std::to_string(*base.requiredStrength));
    if (base.requiredDexterity && *base.requiredDexterity > 0)
        lines.push_back("Required dexterity: " + std::to_string(*base.requiredDexterity));
    if (base.requiredLevel && *base.requiredLevel > 0)
        lines.push_back("Required level: " + std::to_string(*base.requiredLevel));
    int width = 0;
    for (const auto &line : lines)
        width = std::max(width, painter_.measure(line, 12));
    width += 24;
    float height = float(lines.size() * 21 + 18);
    Rectangle box{std::clamp(anchor.x - width, 8.f, W - width - 8.f),
                  std::clamp(anchor.y - height - 12, 60.f, H - HUD - height - 8), float(width), height};
    // Original font and a translucent tooltip; no replacement item illustrations.
    DrawRectangleRec(box, {0, 0, 0, 225});
    for (size_t i = 0; i < lines.size(); ++i)
        painter_.label(lines[i], int(box.x + (box.width - painter_.measure(lines[i], 12)) / 2),
                       int(box.y + 10 + i * 21), 12, i == 0 ? gold : parchment);
}
void SceneView::drawBelt(Vec mouse) const {
    const auto &ui = view_.inventory;
    const auto &inventory = session_.inventory();
    auto belt = inventory.container(session_.playerContainers().belt);
    int rows = ui.open || ui.beltExpanded ? belt->spec.rows : 1;
    auto bounds = beltBounds(rows);
    for (int row = rows - 1; row >= 0; --row) {
        if (auto sprite = assets_.beltPanel.frame(0, 0); sprite && row > 0) {
            auto t = sprite->texture;
            DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
                           hudRect(424, 42 + row * 32, float(t.width), float(t.height)), {0, 0}, 0, WHITE);
        }
        for (int column = 0; column < 4; ++column) {
            auto box = beltSlot({column, row});
            auto item = inventory.item(inventory.itemAt(belt->id, {column, row}));
            if (item) {
                bool dragged = ui.drag && ui.drag->moved && ui.drag->item.id == item->id;
                drawItemIcon(*item, box, dragged ? Fade(WHITE, .3f) : WHITE);
            }
            if (CheckCollisionPointRec(rv(mouse), box))
                DrawRectangleLinesEx(box, 1, parchment);
            if (row == 0)
                painter_.label(std::to_string(column + 1), int(box.x + box.width / 2 - 3), H - 13, 10, gold);
        }
    }
    if (ui.drag && ui.drag->moved) {
        auto drop = inventoryDrop(session_, ui, mouse);
        if (beltCell(mouse, rows)) {
            Color color = drop.error == InventoryError::None ? GREEN : RED;
            DrawRectangleRec(drop.bounds, Fade(color, .18f));
            DrawRectangleLinesEx(drop.bounds, 2, color);
        }
    } else if (auto cell = beltCell(mouse, rows)) {
        if (auto item = inventory.item(inventory.itemAt(belt->id, *cell)))
            drawItemTooltip(*item, {bounds.x + bounds.width + 80, bounds.y - 5});
    }
}
} // namespace d2x
