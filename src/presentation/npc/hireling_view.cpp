#include "presentation/scene_view.hpp"
#include "hireling_panel.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool SceneView::hirelingPortraitVisible() const {
    return hirelingView().active && worldViewport().x == 0 && !view_.blocksWorld();
}
void SceneView::drawHirelingPortrait() const {
    if (worldViewport().x == 0 && !view_.blocksWorld()) {
        const auto &counts = hirelingView().summonCounts;
        float x = hirelingPortraitBounds().x;
        if (hirelingPortraitVisible()) x += hirelingPortraitBounds().width + 12 * classicPanelScale;
        for (const auto &[skill, count] : counts) {
            auto portrait = assets_.summonPortraits.find(skill);
            if (portrait == assets_.summonPortraits.end()) continue;
            const auto *frame = portrait->second.frame(0, 0);
            if (!frame) continue;
            const auto &texture = frame->texture;
            Rectangle bounds{x, hirelingPortraitBounds().y, texture.width * classicPanelScale, texture.height * classicPanelScale};
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds, {0, 0}, 0, WHITE);
            const auto amount = std::to_string(count);
            const int size = std::max(8, int(9 * classicPanelScale));
            painter_.label(amount, int(bounds.x + (bounds.width - painter_.measure(amount, size)) / 2),
                           int(bounds.y + bounds.height + 2 * classicPanelScale), size, WHITE);
            x += bounds.width + 12 * classicPanelScale;
        }
    }
    if (!hirelingPortraitVisible()) return;
    const auto &merc = hirelingView();
    const auto portrait = hirelingPortraitBounds();
    const auto bar = hirelingLifeBounds();
    const float life = std::clamp(merc.life / std::max(1, merc.maximumLife), 0.f, 1.f);
    DrawRectangleRec(bar, {36, 20, 12, 255});
    DrawRectangleRec({bar.x, bar.y, std::floor(bar.width * life), bar.height}, {0, 128, 0, 255});
    if (const auto *frame = assets_.hirelingPortrait.frame(0, 0)) {
        const auto &t = frame->texture;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, portrait, {0, 0}, 0, WHITE);
    }
    const auto &name = merc.name;
    const int size = std::max(8, int(9 * classicPanelScale));
    painter_.label(name, int(portrait.x + (portrait.width - painter_.measure(name, size)) / 2),
                   int(portrait.y + portrait.height + 2 * classicPanelScale), size, WHITE);
}

Rectangle SceneView::hirelingSlotBounds(size_t index) const {
    const auto &box = inventoryView_.hirelingSlots.at(index);
    return hirelingArtRect(float(box[0]), float(box[1]), float(box[2] - box[0]), float(box[3] - box[1]));
}
void SceneView::drawHirelingList(Vec mouse) const {
    if (!view_.hireListOpen) return;
    const auto &list = hirelingListView();
    const auto p = hirelingListBounds();
    DrawRectangleRec(p, {0, 0, 0, 210});
    DrawRectangleLinesEx(p, 1, gold);
    const auto cancel = hirelingListCancel();
    DrawLineEx({p.x, p.y + 42 * classicPanelScale}, {p.x + p.width, p.y + 42 * classicPanelScale}, 1, gold);
    DrawRectangleLinesEx(cancel, 1, gold);
    const auto header = "Your Gold: " + std::to_string(list.gold) + "     Hire which Mercenary?";
    const int size = int(16 * classicPanelScale);
    painter_.label(header, int(p.x + (p.width - painter_.measure(header, size)) / 2),
                   int(p.y + 12 * classicPanelScale), size, gold);
    const auto *offers = &list.offers;
    for (int index = 0; index < hirelingVisibleRows; ++index) {
        const int entry = view_.hireListScroll + index;
        if (entry >= int(offers->size())) break;
        const auto &offer = offers->at(size_t(entry));
        const auto row = hirelingListRow(index);
        const auto color = CheckCollisionPointRec(rv(mouse), row) ? Color{94, 112, 200, 255} : parchment;
        std::string line = offer.name + " - Lvl: " + std::to_string(offer.level) +
            "  Life: " + std::to_string(offer.life) + "  Def: " + std::to_string(offer.defense) +
            "  Cost: " + std::to_string(offer.price);
        int rowSize = size;
        while (rowSize > 9 && painter_.measure(line, rowSize) > row.width) --rowSize;
        painter_.label(line, int(row.x), int(row.y), rowSize, color);
        if (!offer.description.empty())
            painter_.label(offer.description, int(row.x + 20 * classicPanelScale),
                           int(row.y + 18 * classicPanelScale), size, color);
    }
    const auto bar = hirelingScrollBounds();
    DrawRectangleLinesEx(bar, 1, gold);
    auto arrow = [&](int frame, float y) {
        if (const auto *image = assets_.hirelingScroll.frame(0, frame)) {
            const auto &t = image->texture;
            DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
                {bar.x, y, 10 * classicPanelScale, 10 * classicPanelScale}, {0, 0}, 0, WHITE);
        }
    };
    arrow(0, bar.y); arrow(1, bar.y + bar.height - 10 * classicPanelScale);
    const int maximum = offers ? std::max(0, int(offers->size()) - hirelingVisibleRows) : 0;
    arrow(5, bar.y + 11 * classicPanelScale +
        (maximum ? float(view_.hireListScroll) / maximum : 0) * (bar.height - 32 * classicPanelScale));
    const auto label = list.cancelLabel;
    painter_.label(label, int(cancel.x + (cancel.width - painter_.measure(label, size)) / 2),
        int(cancel.y + 12 * classicPanelScale), size,
        CheckCollisionPointRec(rv(mouse), cancel) ? Color{94, 112, 200, 255} : parchment);
}
void SceneView::drawHireling(Vec mouse) const {
    if (!view_.hirelingOpen) return;
    const auto &hireling = hirelingView();
    if (!hireling.known) return;
    const auto p = classicPanelBounds(false);
    drawPanelFrame(false);
    for (int index = 0; index < 4; ++index) {
        const auto *frame = assets_.hirelingPanel.frame(0, index);
        if (!frame) continue;
        const auto &t = frame->texture;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
            {p.x + (index % 2) * 256 * classicPanelScale, p.y + (index / 2) * 256 * classicPanelScale,
             t.width * classicPanelScale, t.height * classicPanelScale}, {0, 0}, 0, WHITE);
    }
    const auto &inventory = inventoryView_;
    auto slots = inventoryView_.containers;
    slots.equipment = slots.hirelingEquipment;
    constexpr EquipmentSlot order[] = {EquipmentSlot::Head, EquipmentSlot::Torso,
                                       EquipmentSlot::RightHand, EquipmentSlot::LeftHand};
    EntityId hovered;
    for (size_t index = 0; index < 4; ++index) {
        const auto box = hirelingSlotBounds(index);
        auto id = inventory.equipped(slots, order[index]);
        bool mirrored = index == 3 && !id;
        if (mirrored) id = inventory.equipped(slots, EquipmentSlot::RightHand);
        if (const auto *item = inventory.item(id)) {
            if (mirrored) DrawRectangleRec(box, {73, 0, 0, 160});
            const bool dragged = view_.inventory.drag &&
                                 view_.inventory.drag->item.id == id;
            if (!dragged)
                drawItemIcon(*item, box, mirrored ? Color{160, 150, 150, 150} : WHITE);
            if (CheckCollisionPointRec(rv(mouse), box)) hovered = id;
        } else {
            const auto &art = index == 0 ? assets_.hirelingHead :
                              index == 1 ? assets_.hirelingArmor : assets_.hirelingWeapon;
            if (const auto *frame = art.frame(0, 0)) {
                const auto &t = frame->texture;
                DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, box, {0, 0}, 0, WHITE);
            }
        }
    }
    auto cell = [&](const std::string &text, float x, float y, float width, float height, bool right = false) {
        const auto box = hirelingArtRect(x, y, width, height);
        int size = int(9 * classicPanelScale);
        while (size > 7 && painter_.measure(text, size) > box.width - 6) --size;
        painter_.label(text, int(right ? box.x + box.width - painter_.measure(text, size) - 4 : box.x + 4),
                       int(box.y + (box.height - size) / 2), size, parchment);
    };
    cell(hireling.name, 5, 199, 150, 17);
    cell("Life", 161, 199, 51, 17);
    cell(std::to_string(int(hireling.life)) + " / " + std::to_string(hireling.maximumLife), 212, 199, 98, 17, true);
    cell("Experience", 7, 222, 121, 14);
    auto number = [](uint64_t value) {
        auto text = std::to_string(value);
        for (int index = int(text.size()) - 3; index > 0; index -= 3) text.insert(size_t(index), ",");
        return text;
    };
    cell(number(hireling.experience), 7, 238, 121, 18, true);
    cell("Level", 134, 222, 45, 14);
    cell(std::to_string(hireling.level), 134, 238, 45, 18, true);
    cell("Next Level", 186, 222, 123, 14);
    cell(number(hireling.nextExperience), 186, 238, 123, 18, true);
    const char *labels[] = {"Strength", "Dexterity", "Damage", "Defense"};
    const std::string values[] = {std::to_string(hireling.strength), std::to_string(hireling.dexterity),
        std::to_string(hireling.damageMinimum) + "-" + std::to_string(hireling.damageMaximum), std::to_string(hireling.defense)};
    const char *resists[] = {"Fire", "Cold", "Lightning", "Poison"};
    const auto &numbers = hireling.resistances;
    for (int index = 0; index < 4; ++index) {
        const float y = 265.f + 24.f * index;
        cell(labels[index], 7, y, 90, 18); cell(values[index], 99, y, 55, 18, true);
        cell(resists[index], 164, y - 3, 98, 12); cell("Resistance", 164, y + 7, 98, 12);
        cell(std::to_string(numbers[index]), 264, y, 43, 18, true);
    }
    const auto close = hirelingClose();
    const auto *button = assets_.questClose.frame(0, CheckCollisionPointRec(rv(mouse), close) ? 11 : 10);
    if (button) {
        const auto &t = button->texture;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, close, {0, 0}, 0, WHITE);
    }
    if (view_.inventory.drag && view_.inventory.drag->moved &&
        CheckCollisionPointRec(rv(mouse), classicSideBounds(false))) {
        const auto drop = inventoryDrop(inventoryView_, inventoryClient_, view_.inventory, mouse, true);
        if (drop.bounds.width > 0)
            DrawRectangleLinesEx(drop.bounds, 2,
                drop.error == InventoryError::None ? Color{99, 202, 118, 255} : Color{240, 91, 68, 255});
    } else if (!view_.inventory.drag)
        if (const auto *item = inventory.item(hovered)) drawItemTooltip(*item, mouse);
}
} // namespace d2x
