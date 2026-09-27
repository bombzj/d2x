#include "scene_view.hpp"
#include "content/item_properties.hpp"
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
    auto localized = [&](const std::string &key) {
        const auto &strings = session_.content().itemStrings;
        auto found = strings.find(key);
        return found == strings.end() ? key : found->second;
    };
    if (!item.identified) {
        const auto *definition = session_.inventory().catalog().find(item.definition);
        return definition ? definition->name : item.definition;
    }
    if (auto special = specialItem(item))
        return localized(special->name);
    const auto *definition = session_.inventory().catalog().find(item.definition);
    std::string name = definition ? definition->name : item.definition;
    if (item.quality == ItemQuality::Superior)
        return "Superior " + name;
    if (item.quality == ItemQuality::Inferior) {
        auto found = std::find_if(session_.content().inferiorGrades.begin(),
                                  session_.content().inferiorGrades.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
        if (found != session_.content().inferiorGrades.end())
            return localized(found->name) + " " + name;
    }
    if (item.quality == ItemQuality::Rare) {
        auto rareName = [&](const auto &records, int32_t row) -> std::string {
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == row; });
            return found == records.end() ? std::string{} : localized(found->name);
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
                name = affix.prefix ? localized(found->name) + " " + name : name + " " + localized(found->name);
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
void SceneView::drawItemTooltip(const ItemInstance &item, Vec anchor,
                                std::optional<unsigned> price, bool gamble,
                                std::string_view priceLabel) const {
    const auto &definition = *session_.inventory().catalog().find(item.definition);
    std::vector<std::string> lines{itemName(item)};
    std::vector<Color> colors;
    const Color nameColor = itemColor(item.quality);
    colors.push_back(nameColor);
    auto line = [&](std::string value, Color color = WHITE) {
        if (!value.empty()) { lines.push_back(std::move(value)); colors.push_back(color); }
    };
    if (price) line(std::string(priceLabel) + ": " + std::to_string(*price));
    if (!gamble) {
        if ((item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set ||
             item.quality == ItemQuality::Rare) && item.identified) line(definition.name, nameColor);
        const auto stats = resolveItemStats(session_.content(), item, session_.state().player.level);
        auto sum = [&](const char *name) {
            int result = 0;
            for (const auto &stat : stats) if (stat.effect == name) result += stat.value;
            return result;
        };
        const auto &base = definition.base;
        auto damage = [&](const char *label, std::optional<int> low, std::optional<int> high, bool thrown = false) {
            if (!low || !high) return;
            if (item.quality == ItemQuality::Inferior) {
                low = std::max(thrown ? 2 : 1, *low * 75 / 100);
                high = std::max(thrown ? 1 : 2, *high * 75 / 100);
            }
            const int minimum = std::max(1, *low * (100 + sum("item_mindamage_percent")) / 100 + sum("mindamage"));
            const int maximum = std::max(minimum + 1, *high * (100 + sum("item_maxdamage_percent")) / 100 + sum("maxdamage"));
            line(std::string(label) + std::to_string(minimum) + " - " + std::to_string(maximum));
        };
        if (definition.family == ItemFamily::Weapon) {
            if (definition.equipment.throwable) damage("Throw Damage: ", base.throwMin, base.throwMax, true);
            if (!definition.equipment.twoHanded || definition.equipment.oneOrTwoHanded)
                damage("One-Hand Damage: ", base.minDamage, base.maxDamage);
            if (definition.equipment.twoHanded)
                damage("Two-Hand Damage: ", base.twoHandMin, base.twoHandMax);
        }
        if (definition.family == ItemFamily::Armor) {
            const int percent = sum("item_armor_percent");
            const int baseArmor = percent ? base.maxDefense.value_or(item.defense) + 1 : item.defense;
            line("Defense: " + std::to_string(baseArmor * std::max(0, 100 + percent) / 100 + sum("armorclass")));
        }
        if (definition.maxStack > 1 && !definition.equipment.isType("gold"))
            line("Quantity: " + std::to_string(item.quantity));
        if (definition.bookCapacity)
            line("Quantity: " + std::to_string(item.charges));
        if (definition.maxDurability && !definition.equipment.throwable)
            line("Durability: " + std::to_string(item.durability) + " of " +
                 std::to_string(session_.inventory().maximumDurability(item)));
        if (definition.beltRows)
            line("Belt capacity: " + std::to_string(4 * definition.beltRows));
        if (const auto *potion = session_.content().potion(item.definition)) {
            if (potion->kind == PotionKind::Healing)
                line("Heals " + std::to_string(int(potion->amount)) + " Life");
            else if (potion->kind == PotionKind::Mana)
                line("Restores " + std::to_string(int(potion->amount)) + " Mana");
            else if (potion->kind == PotionKind::Rejuvenation)
                line("Restores " + std::to_string(int(potion->amount * 100)) + "% Life and Mana");
            else if (potion->kind == PotionKind::Stamina)
                line("Restores Stamina");
            if (potion->curesPoison) line("Cures Poison");
            if (potion->curesCold) line("Cures Cold");
            if (potion->seconds > 0) line("Duration: " + std::to_string(int(potion->seconds)) + " seconds");
        }
        const auto actor = session_.characterStats();
        auto requirement = [&](const char *label, int required, int actual) {
            if (required > 0) line(std::string(label) + std::to_string(required), actual < required ? RED : WHITE);
        };
        auto required = [&](std::optional<int> value) {
            const int baseValue = value.value_or(0);
            return std::max(0, baseValue + baseValue * sum("item_req_percent") / 100);
        };
        requirement("Required Strength: ", required(base.requiredStrength), actor.strength);
        requirement("Required Dexterity: ", required(base.requiredDexterity), actor.dexterity);
        requirement("Required Level: ", std::max(base.requiredLevel.value_or(0), item.requiredLevel), session_.state().player.level);
        if (!item.identified) line("Unidentified", RED);
        else {
            for (auto &description : describeItemStats(session_.content(), item, session_.state().player.level))
                line(std::move(description), {105, 105, 255, 255});
            if (item.grantedSkill >= 0)
                if (const auto *skill = session_.content().skills.find(item.grantedSkill))
                    line("+1 to " + skill->name, {105, 105, 255, 255});
        }
        if (item.definition == "bkd") {
            line("Cairn Stones order:");
            for (int objectClass : session_.cainStoneSequence())
                for (const auto &region : session_.regions())
                    for (const auto &object : region.objects)
                        if (object.interaction == Interaction::QuestStone && object.objectClass == objectClass)
                            line(object.name);
        }
    }
    int fontSize = 16, rowHeight = 20;
    size_t rowsPerColumn = 0, columns = 0;
    std::vector<int> widths;
    int width = 0;
    for (;;) {
        rowHeight = fontSize + 4;
        rowsPerColumn = size_t((H - HUD - 34) / rowHeight);
        columns = (lines.size() + rowsPerColumn - 1) / rowsPerColumn;
        widths.assign(columns, 0);
        for (size_t index = 0; index < lines.size(); ++index)
            widths[index / rowsPerColumn] = std::max(widths[index / rowsPerColumn],
                painter_.measure(lines[index], fontSize) + 24);
        width = 0;
        for (int size : widths) width += size;
        if (width <= W - 16 || fontSize == 1) break;
        --fontSize;
    }
    const float height = float(std::min(lines.size(), rowsPerColumn) * rowHeight + 18);
    Rectangle box{std::clamp(anchor.x - width, 8.f, std::max(8.f, W - width - 8.f)),
                  std::clamp(anchor.y - height - 12, 8.f, std::max(8.f, H - HUD - height - 8.f)),
                  float(width), height};
    DrawRectangleRec(box, {0, 0, 0, 225});
    int columnX = int(box.x);
    for (size_t column = 0; column < columns; ++column) {
        for (size_t row = 0; row < rowsPerColumn && column * rowsPerColumn + row < lines.size(); ++row) {
            size_t i = column * rowsPerColumn + row;
            painter_.inBox(lines[i], {float(columnX), box.y + 9 + float(row * rowHeight),
                                      float(widths[column]), float(rowHeight)}, fontSize, colors[i]);
        }
        columnX += widths[column];
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
                bool dragged = ui.drag && ui.drag->item.id == item->id;
                if (!dragged) drawItemIcon(*item, box);
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
