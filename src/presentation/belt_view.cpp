#include "scene_view.hpp"
#include <algorithm>
#include <optional>
#include <span>

namespace d2x {
const SpecialItemRecord *SceneView::specialItem(const ItemInstance &item) const {
    if (item.specialRow < 0)
        return nullptr;
    const auto &records = item.quality == ItemQuality::Unique ? session_.content().uniqueItems
                                                                : session_.content().setItems;
    auto found = std::find_if(records.begin(), records.end(),
                              [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
    return found == records.end() ? nullptr : &*found;
}
std::string SceneView::itemName(const ItemInstance &item) const {
    if (auto special = specialItem(item))
        return special->name;
    const auto *definition = session_.inventory().catalog().find(item.definition);
    std::string name = definition ? definition->name : item.definition;
    if (item.quality == ItemQuality::Superior)
        return "Superior " + name;
    if (item.quality == ItemQuality::Inferior) {
        auto found = std::find_if(session_.content().inferiorGrades.begin(),
                                  session_.content().inferiorGrades.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
        if (found != session_.content().inferiorGrades.end())
            return found->name + " " + name;
    }
    if (item.quality == ItemQuality::Rare) {
        auto rareName = [](const auto &records, int32_t row) -> std::string {
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == row; });
            return found == records.end() ? std::string{} : found->name;
        };
        auto prefix = rareName(session_.content().rarePrefixes, item.rarePrefixRow);
        auto suffix = rareName(session_.content().rareSuffixes, item.rareSuffixRow);
        if (!prefix.empty() && !suffix.empty())
            return prefix + " " + suffix;
    }
    if (item.quality == ItemQuality::Magic)
        for (const auto &affix : item.affixes) {
            const auto &records = affix.prefix ? session_.content().magicPrefixes
                                                : session_.content().magicSuffixes;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == affix.row; });
            if (found != records.end())
                name = affix.prefix ? found->name + " " + name : name + " " + found->name;
        }
    return name;
}
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
    std::vector<std::string> lines{itemName(item)};
    const char *qualities[] = {"Normal", "Magic", "Rare", "Set", "Unique", "Superior", "Inferior"};
    auto quality = size_t(item.quality);
    lines.push_back(std::string(quality < std::size(qualities) ? qualities[quality] : "Unknown") +
                    " / Item level " + std::to_string(item.level));
    if (auto potion = session_.content().potion(item.definition)) {
        switch (potion->kind) {
        case PotionKind::Healing:
            lines.push_back("Life: +" + std::to_string(int(potion->amount)) + " over " +
                            std::to_string(potion->seconds) + " seconds");
            break;
        case PotionKind::Mana:
            lines.push_back("Mana: +" + std::to_string(int(potion->amount)) + " over " +
                            std::to_string(potion->seconds) + " seconds");
            break;
        case PotionKind::Rejuvenation:
            lines.push_back("Instant life and mana: " + std::to_string(int(potion->amount * 100)) + "%");
            break;
        case PotionKind::Stamina:
            lines.push_back("Full stamina; no drain for " + std::to_string(int(potion->seconds)) +
                            " seconds");
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
    if (definition.maxDurability && !item.durability)
        lines.push_back("Broken");
    const auto &base = definition.base;
    if (!definition.equipment.requiredClass.empty())
        lines.push_back("Class: " + definition.equipment.requiredClass);
    if (definition.family == ItemFamily::Weapon && base.minDamage && base.maxDamage)
        lines.push_back("Base damage: " + std::to_string(*base.minDamage) + " - " +
                        std::to_string(*base.maxDamage));
    if (definition.equipment.throwable && base.throwMin && base.throwMax)
        lines.push_back("Throw damage: " + std::to_string(*base.throwMin) + " - " +
                        std::to_string(*base.throwMax));
    if (definition.family == ItemFamily::Armor && base.minDefense && base.maxDefense)
        lines.push_back("Defense: " + std::to_string(item.defense));
    if (base.requiredStrength && *base.requiredStrength > 0)
        lines.push_back("Required strength: " + std::to_string(*base.requiredStrength));
    if (base.requiredDexterity && *base.requiredDexterity > 0)
        lines.push_back("Required dexterity: " + std::to_string(*base.requiredDexterity));
    if (int level = std::max(base.requiredLevel.value_or(0), item.requiredLevel); level > 0)
        lines.push_back("Required level: " + std::to_string(level));
    auto propertyText = [](const PropertyRange &property, std::optional<int32_t> value) {
        std::string line = property.code;
        if (!property.parameter.empty())
            line += " (" + property.parameter + ")";
        if (property.directRoll && value)
            line += ": " + std::to_string(*value);
        else if (property.minimum || property.maximum)
            line += ": source " + std::to_string(property.minimum.value_or(0)) + " / " +
                    std::to_string(property.maximum.value_or(0));
        return line;
    };
    auto showProperties = [&](std::span<const PropertyRange> properties,
                              std::span<const int32_t> values) {
        for (size_t index = 0; index < properties.size(); ++index)
            lines.push_back(propertyText(properties[index], values[index]));
    };
    if (auto special = specialItem(item))
        showProperties(special->properties, item.propertyRolls);
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? session_.content().magicPrefixes
                                            : session_.content().magicSuffixes;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return int32_t(record.row) == affix.row; });
        if (found != records.end())
            showProperties(found->properties, affix.propertyRolls);
    }
    if (item.quality == ItemQuality::Superior)
        for (const auto &grade : session_.content().superiorGrades)
            if (int32_t(grade.row) == item.gradeRow) {
                showProperties(grade.properties, item.propertyRolls);
                break;
            }
    if (item.quality == ItemQuality::Set)
        if (auto special = specialItem(item))
            for (const auto &bonus : special->setBonuses)
                lines.push_back(bonus.condition + ": " + propertyText(bonus.property, std::nullopt));
    if (specialItem(item) || !item.affixes.empty() || item.gradeRow >= 0)
        lines.push_back("Listed properties are not active in combat yet");
    constexpr size_t rowsPerColumn = 28;
    constexpr int rowHeight = 18;
    const size_t columns = (lines.size() + rowsPerColumn - 1) / rowsPerColumn;
    std::vector<int> columnWidths(columns, 0);
    for (size_t i = 0; i < lines.size(); ++i)
        columnWidths[i / rowsPerColumn] =
            std::max(columnWidths[i / rowsPerColumn], painter_.measure(lines[i], 12) + 24);
    int width = 0;
    for (int columnWidth : columnWidths)
        width += columnWidth;
    float height = float(std::min(lines.size(), rowsPerColumn) * rowHeight + 18);
    Rectangle box{std::clamp(anchor.x - width, 8.f, std::max(8.f, W - width - 8.f)),
                  std::clamp(anchor.y - height - 12, 60.f,
                             std::max(60.f, H - HUD - height - 8.f)), float(width), height};
    // Original font and a translucent tooltip; no replacement item illustrations.
    DrawRectangleRec(box, {0, 0, 0, 225});
    int columnX = int(box.x);
    for (size_t column = 0; column < columns; ++column) {
        for (size_t row = 0; row < rowsPerColumn && column * rowsPerColumn + row < lines.size(); ++row) {
            size_t index = column * rowsPerColumn + row;
            painter_.label(lines[index],
                           columnX + (columnWidths[column] - painter_.measure(lines[index], 12)) / 2,
                           int(box.y + 10 + row * rowHeight), 12, index == 0 ? gold : parchment);
        }
        columnX += columnWidths[column];
    }
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
