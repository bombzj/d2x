#include "presentation/scene_view.hpp"
#include "hireling_panel.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace d2x {
namespace {
enum class HirelingTextAlign { Left, Center, Right };
void hirelingText(const ClassicFont &font,const std::string &value,float left,float baseline,float right,HirelingTextAlign align) {
    std::vector<std::string> lines;std::istringstream input(value);std::string line;
    while(std::getline(input,line)) lines.push_back(line);
    const auto clip=hirelingArtRect(left,baseline-20,right-left+1,40);
    BeginScissorMode(int(clip.x),int(clip.y),int(clip.width),int(clip.height));
    for(size_t row=0;row<lines.size();++row) {
        float width=0;for(unsigned char ch:lines[row]) width+=font.widths[ch];
        const float inset=align==HirelingTextAlign::Left?0:std::max(0.f,right-left+1-width)*(align==HirelingTextAlign::Center?.5f:1.f);
        const auto origin=hirelingArtRect(left+std::floor(inset),baseline+(float(row)-(float(lines.size())-1)*.5f)*8,0,0);
        float x=origin.x;
        for(unsigned char ch:lines[row]) {
            if(const auto *glyph=font.glyphs.frame(0,font.indices[ch]);glyph && ch!=' ')
                DrawTexturePro(glyph->texture,{0,0,float(glyph->texture.width),float(glyph->texture.height)},
                    {x+glyph->x*classicPanelScale,origin.y+(glyph->y-glyph->texture.height)*classicPanelScale,
                        glyph->texture.width*classicPanelScale,glyph->texture.height*classicPanelScale},{},0,WHITE);
            x+=font.widths[ch]*classicPanelScale;
        }
    }
    EndScissorMode();
}
}
bool SceneView::hirelingPortraitVisible() const {
    return hirelingView().active && worldViewport().x == 0 && !view_.capturesWorldInput();
}
void SceneView::drawHirelingPortrait() const {
    if (worldViewport().x == 0 && !view_.capturesWorldInput()) {
        const auto &counts = hirelingView().summonCounts;
        float x = hirelingPortraitBounds(partyPortraitOffset()).x;
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
    const auto portrait = hirelingPortraitBounds(partyPortraitOffset());
    const auto bar = hirelingLifeBounds(partyPortraitOffset());
    const float life = std::clamp(merc.life / std::max(1, merc.maximumLife), 0.f, 1.f);
    DrawRectangleRec(bar, {36, 20, 12, 255});
    DrawRectangleRec({bar.x, bar.y, std::floor(bar.width * life), bar.height}, {0, 128, 0, 255});
    const auto art = assets_.hirelingPortraits.find(merc.classId);
    if (const auto *frame = art != assets_.hirelingPortraits.end() ? art->second.frame(0, 0) : nullptr) {
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
    const auto p = hirelingPanelBounds();
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
    const auto drop = inventoryDrop(inventoryView_, inventoryClient_, view_.inventory, mouse, true);
    for (size_t index = 0; index < 4; ++index) {
        const auto box = hirelingSlotBounds(index);
        auto id = inventory.equipped(slots, order[index]);
        if (const auto *item = inventory.item(id)) {
            drawInventoryDrop(drop, box);
            const bool dragged = view_.inventory.hidesItem(*item);
            if (!dragged)
                drawItemIcon(*item, box);
            if (!dragged && CheckCollisionPointRec(rv(mouse), box)) hovered = id;
        } else {
            const auto *art = index == 0 ? &assets_.hirelingHead :
                              index == 1 ? &assets_.hirelingArmor : index == 2 ? &assets_.hirelingWeapon : nullptr;
            if (const auto *frame = art ? art->frame(0, 0) : nullptr) {
                const auto &t = frame->texture;
                DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, box, {0, 0}, 0, WHITE);
            }
            drawInventoryDrop(drop, box);
        }
    }
    const auto label = [&](const std::string &text,float left,float baseline,float right,HirelingTextAlign align=HirelingTextAlign::Left) {
        hirelingText(assets_.characterLabelFont,text,left,baseline,right,align);
    };
    const auto value = [&](const std::string &text,float left,float baseline,float right,const ClassicFont *color=nullptr,HirelingTextAlign align=HirelingTextAlign::Right) {
        const auto &font=color?*color:assets_.characterCompactFont;
        hirelingText(font,text,left,baseline,right,align);
    };
    const auto &labels=assets_.characterLabels;
    label(hireling.name,17,214,151);
    label(labels.at("strchrlif"),183,214,211);
    value(std::to_string(int(hireling.life))+" / "+std::to_string(hireling.maximumLife),212,215,310,nullptr,HirelingTextAlign::Center);
    label(labels.at("strchrexp"),17,235,127);
    auto number = [](uint64_t value) {
        auto text = std::to_string(value);
        for (int index = int(text.size()) - 3; index > 0; index -= 3) text.insert(size_t(index), ",");
        return text;
    };
    value(number(hireling.experience),7,254,124);
    label(labels.at("strchrlvl"),142,235,182);
    value(std::to_string(hireling.level),134,254,177);
    label(labels.at("strchrnxtlvl"),196,235,310);
    value(number(hireling.nextExperience),192,254,310);
    const char *attributes[] = {"strchrstr", "strchrdex", "strchrskm", "strchrdef"};
    const std::string values[] = {std::to_string(hireling.strength), std::to_string(hireling.dexterity),
        std::to_string(hireling.damageMinimum) + "-" + std::to_string(hireling.damageMaximum), std::to_string(hireling.defense)};
    const char *resists[] = {"strchrfir", "strchrcol", "strchrlit", "strchrpos"};
    const auto &numbers = hireling.resistances;
    for (int index = 0; index < 4; ++index) {
        const float y = 280.f + 24.f * index;
        label(labels.at(attributes[index]),17,y,99);
        value(values[index],106,y+1,154,index==3 && hireling.defenseImproved?&assets_.hirelingBlueFont:nullptr);
        label(labels.at(resists[index]),164,y,263,HirelingTextAlign::Center);
        value(std::to_string(numbers[index]),266,y+1,310,numbers[index]<0?&assets_.hirelingRedFont:nullptr);
    }
    const auto close = hirelingClose();
    const auto *button = assets_.questClose.frame(0, CheckCollisionPointRec(rv(mouse), close) ? 11 : 10);
    if (button) {
        const auto &t = button->texture;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, close, {0, 0}, 0, WHITE);
    }
    if (!view_.inventory.drag)
        if (const auto *item = inventory.item(hovered)) drawItemTooltip(*item, mouse);
}
} // namespace d2x
