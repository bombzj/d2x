#include "presentation/scene_view.hpp"
#include "hireling_panel.hpp"
#include "presentation/graphics/text_list.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace d2x {
constexpr std::array<EquipmentSlot, 7> hirelingEquipmentOrder{EquipmentSlot::Head, EquipmentSlot::Torso,
    EquipmentSlot::RightHand, EquipmentSlot::LeftHand, EquipmentSlot::Belt, EquipmentSlot::Feet, EquipmentSlot::Gloves};
namespace {
struct HirelingConfirmationMenu {
    const std::string &title;
    std::array<std::string, 2> choices;
    Rectangle bounds;
};
HirelingConfirmationMenu hirelingConfirmationMenu(const OriginalMenu &menu,
    const std::map<std::string,std::string,std::less<>> &labels, Rectangle viewport) {
    const auto &title = labels.at("VerifyTransaction9");
    std::array choices{labels.at("VerifyTransaction1"), labels.at("VerifyTransaction2")};
    const auto height = menu.size(title, choices).y;
    const auto bounds = menu.bounds({viewport.x + viewport.width/2, viewport.y + (viewport.height-height)/2},
        viewport, title, choices);
    return {title, std::move(choices), bounds};
}
enum class HirelingTextAlign { Left, Center, Right };
void hirelingText(const ClassicFont &font, const std::string &value, float left, float baseline, float right,
                  HirelingTextAlign align) {
    std::vector<std::string> lines;
    std::istringstream input(value);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    const auto clip = hirelingPanelRect(left, baseline - 20, right - left + 1, 40);
    uiScissor(clip.x, clip.y, clip.width, clip.height);
    for (size_t row = 0; row < lines.size(); ++row) {
        const float width = font.textWidth(lines[row]);
        const float room = std::max(0.f, right - left + 1 - width);
        const float inset = align == HirelingTextAlign::Left ? 0 : align == HirelingTextAlign::Center ? room / 2 : room;
        const auto origin = hirelingPanelRect(left + std::floor(inset),
            baseline + (float(row) - (float(lines.size()) - 1) * .5f) * 8, 0, 0);
        font.drawBaseline(lines[row], {origin.x, origin.y}, classicPanelScale());
    }
    EndScissorMode();
}
}
Rectangle SceneView::hirelingSlotBounds(size_t index) const {
    const auto &box = inventoryView_.hirelingSlots.at(index);
    return hirelingPanelRect(float(box[0]), float(box[1]), float(box[2] - box[0]), float(box[3] - box[1]));
}
int SceneView::hirelingConfirmationAt(Vec mouse) const {
    const auto menu = hirelingConfirmationMenu(originalMenu_, assets_.characterLabels, worldViewport());
    return originalMenu_.hit(menu.bounds, true, menu.choices.size(), mouse);
}
Rectangle SceneView::hirelingCancelBounds() const {
    return OriginalMenu::buttonRow(hirelingListBounds(), hirelingListCancelBaseline());
}
void SceneView::drawHirelingList(Vec mouse) const {
    if (!view_.hireListOpen) return;
    const auto &list = hirelingListView();
    if (view_.hirelingConfirmation) {
        const auto menu = hirelingConfirmationMenu(originalMenu_, assets_.characterLabels, worldViewport());
        originalMenu_.draw(menu.bounds, menu.title, menu.choices,
            originalMenu_.hit(menu.bounds, true, menu.choices.size(), mouse));
        return;
    }
    if (list.hirePending) {
        const auto &text = assets_.characterLabels.at("TransactionResults1");
        const auto viewport = worldViewport();
        const auto box = originalMenu_.bounds({viewport.x + viewport.width/2, viewport.y + viewport.height/2},
            viewport, text, {});
        originalMenu_.draw(box, text, {});
        return;
    }
    const auto p = hirelingListBounds();
    const auto bounds = hirelingListContentBounds();
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette ? actPaletteBlends_[size_t(palette)].get() : paletteBlend_.get();
    originalMenu_.drawListFrame(p, bounds, blend);
    const auto &labels = assets_.hirelingMenuText;
    std::string header = labels.title;
    if (const auto at = header.find("%d"); at != std::string::npos)
        header.replace(at, 2, std::to_string(list.gold));
    originalMenu_.label(header, {p.x, p.y + 10, p.width, 21}, 4);
    originalMenu_.drawButton(labels.cancel, p, hirelingListCancelBaseline(), mouse);

    const auto &layout = hirelingListLayout();
    const int maximum = layout.lastPageFirst();
    const int first = std::clamp(view_.hireListScroll, 0, maximum);
    const int visible = layout.pageSize(first);
    const int hovered = layout.hit(first, mouse);
    const int hoveredOffer = hovered < 0 ? -1 : layout.rows[size_t(hovered)].offer;
    const float x = bounds.x + textListInset;
    const float firstBaseline = bounds.y + textListBaseline;
    uiScissor(bounds.x, bounds.y, bounds.width - 18, bounds.height - 11);
    if (list.listReceived && list.offers.empty()) {
        float baseline = firstBaseline;
        for (const auto &line : wrapClassicText(labels.empty, assets_.font, textListWidth(bounds))) {
            assets_.font.drawBaseline(line, {x, baseline}, 1);
            baseline += textListLineStep;
        }
    }
    for (int index = first, top = 0; index < first + visible; ++index) {
        const auto &row = layout.rows[size_t(index)];
        const auto &font = assets_.itemTextFonts[row.offer == hoveredOffer ? 3 : 0];
        for (int line = 0; line < int(row.lines.size()); ++line)
            font.drawBaseline(row.lines[size_t(line)], {x, firstBaseline + top + line * textListLineStep}, 1);
        top += hirelingRowExtent(int(row.lines.size()));
    }
    EndScissorMode();
    TextSlider::list(bounds, false).draw(assets_.textSlider, view_.hireListSlider, first, maximum,
        int(layout.rows.size()), visible);
}
void SceneView::drawHireling(Vec mouse) const {
    if (!view_.hirelingOpen) return;
    const auto &hireling = hirelingView();
    if (!hireling.known) return;
    const auto p = hirelingPanelBounds();
    drawPanelFrame(false);
    for (int index = 0; index < 4; ++index) {
        const auto *frame = assets_.hirelingPanel.frame(0, index);
        if (!frame) continue;
        const auto &t = frame->texture;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
            {p.x + (index % 2) * 256 * classicPanelScale(), p.y + (index / 2) * 256 * classicPanelScale(),
             t.width * classicPanelScale(), t.height * classicPanelScale()}, {0, 0}, 0, WHITE);
    }
    const auto &inventory = inventoryView_;
    auto slots = inventoryView_.containers;
    slots.equipment = slots.hirelingEquipment;
    const auto &order = hirelingEquipmentOrder;
    const auto *weapon = inventory.item(inventory.equipped(slots, EquipmentSlot::RightHand));
    const auto *weaponDefinition = weapon ? inventory.definition(weapon->definition) : nullptr;
    const bool twoHanded = weaponDefinition && weaponDefinition->twoHanded;
    EntityId hovered;
    const auto drop = inventoryDropAt(mouse);
    for (size_t index = 0; index < order.size(); ++index) {
        if (inventory.hirelingSlots[index][0] < 0) continue;
        const auto box = hirelingSlotBounds(index);
        auto id = inventory.equipped(slots, order[index]);
        if (const auto *item = inventory.item(id)) {
            drawInventoryDrop(drop, box);
            const bool dragged = view_.inventory.hidesItem(*item);
            if (!dragged) {
                const bool over = !view_.inventory.drag && CheckCollisionPointRec(rv(mouse), box);
                if (over) drawItemBackground(*item, box, true);
                drawItemIcon(*item, box, over);
            }
            if (!dragged && CheckCollisionPointRec(rv(mouse), box)) hovered = id;
        } else {
            const auto slot = order[index];
            const bool head = slot == EquipmentSlot::Head;
            const bool torso = slot == EquipmentSlot::Torso;
            const bool hand = slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand;
            const auto *art = head ? &assets_.hirelingHead : torso ? &assets_.hirelingArmor :
                hand && !twoHanded ? &assets_.hirelingWeapon : nullptr;
            if (const auto *frame = art ? art->frame(0, head ? 1 : 0) : nullptr) {
                const auto &t = frame->texture;
                const float scale = classicPanelScale();
                const Rectangle image{box.x + (torso ? 2 : 0) * scale,
                    box.y + box.height - (t.height + (head ? 0 : 1)) * scale,
                    t.width * scale, t.height * scale};
                DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, image, {}, 0, WHITE);
            }
            drawInventoryDrop(drop, box);
        }
    }
    const auto label = [&](const std::string &text,float left,float baseline,float right,HirelingTextAlign align=HirelingTextAlign::Left) {
        hirelingText(assets_.characterLabelFont,text,left,baseline,right,align);
    };
    const auto value = [&](const std::string &text, float left, float baseline, float right,
                           int color = 0, HirelingTextAlign align = HirelingTextAlign::Right, int maximumWidth = 0) {
        const ClassicFont *regular = &assets_.characterCompactFont;
        if (color == 3) regular = &assets_.hirelingBlueFont;
        if (color == 1) regular = &assets_.hirelingRedFont;
        if (color == 4) regular = &assets_.hirelingGoldFont;
        const auto &font = maximumWidth && regular->textWidth(text) > maximumWidth ?
            assets_.automapTextFonts[size_t(color)] : *regular;
        hirelingText(font,text,left,baseline,right,align);
    };
    const auto &labels=assets_.characterLabels;
    // 1.13c 40EB0/3C8B0/3EA20 use separate records for the name, labels and values.
    label(hireling.name, 15, 214, 174);
    label(labels.at("strchrlif"), 180, 214, 209);
    value(std::to_string(int(hireling.life)) + " / " + std::to_string(hireling.maximumLife),
        210, 215, 305, 0, HirelingTextAlign::Center);
    label(labels.at("strchrexp"), 15, 236, 127);
    value(std::to_string(hireling.experience), 7, 254, 119);
    label(labels.at("strchrlvl"), 145, 236, 188);
    value(std::to_string(hireling.level), 134, 254, 174);
    label(labels.at("strchrnxtlvl"), 200, 236, 310);
    value(std::to_string(hireling.nextExperience), 192, 254, 304);
    const char *attributes[] = {"strchrstr", "strchrdex", "strchrskm", "strchrdef"};
    const std::string values[] = {std::to_string(hireling.strength), std::to_string(hireling.dexterity),
        std::to_string(hireling.damageMinimum) + "-" + std::to_string(hireling.damageMaximum), std::to_string(hireling.defense)};
    const char *resists[] = {"strchrfir", "strchrcol", "strchrlit", "strchrpos"};
    const auto &numbers = hireling.resistances;
    for (int index = 0; index < 4; ++index) {
        const float y = 281.f + 24.f * index;
        label(labels.at(attributes[index]), 15, y, 104);
        value(values[index], 106, y + 1, 153, index == 3 && hireling.defenseImproved ? 3 : 0,
            HirelingTextAlign::Right, 48);
        label(labels.at(resists[index]), 180, y, 245, HirelingTextAlign::Center);
        const int color = hireling.resistanceCapped[index] ? 4 : numbers[index] < 0 ? 1 : 0;
        value(std::to_string(numbers[index]), 261, y + 1, 308, color, HirelingTextAlign::Right, 48);
    }
    for(size_t index=0;index<hireling.skills.size() && index<6;++index) {
        const float left=index%2?160.f:17.f;
        value(hireling.skills[index],left,379.f+12.f*float(index/2),index%2?270.f:154.f,0,HirelingTextAlign::Left);
    }
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette ? actPaletteBlends_[size_t(palette)].get() : paletteBlend_.get();
    drawClassicSpriteButton(assets_.questClose, 10, hirelingCloseArt(), hirelingCloseHit(),
        mouse, view_.hirelingCloseButton.pressed, blend);
    if (!view_.inventory.drag)
        if (const auto *item = inventory.item(hovered)) drawItemTooltip(*item, mouse);
}
} // namespace d2x
