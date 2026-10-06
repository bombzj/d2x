#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
Color SceneView::itemColor(ItemQuality quality) {
    switch (quality) {
    case ItemQuality::Magic:
        return {105, 105, 255, 255};
    case ItemQuality::Crafted:
        return {255, 168, 0, 255};
    case ItemQuality::Rare:
        return {255, 255, 100, 255};
    case ItemQuality::Set:
        return {0, 255, 0, 255};
    case ItemQuality::Unique:
        return {199, 179, 119, 255};
    default:
        return WHITE;
    }
}
const Sprite *SceneView::groundItemSprite(const InventoryItemView &item) const {
    const auto key=item.groundArt.empty()?item.artKey:item.artKey+":"+item.groundArt;
    auto found=assets_.itemGround.find(key);
    if(found==assets_.itemGround.end() || found->second.count<=0) return nullptr;
    const int frame=found->second.count-1;
    return found->second.frame(0,frame);
}
void SceneView::drawGroundItem(EntityId id, bool highlighted) const {
    const auto *item = inventoryView_.item(id);
    if (item) drawGroundItem(*item, highlighted);
}
void SceneView::drawGroundItem(const InventoryItemView &item, bool highlighted) const {
    auto p = screen(staticUnitPosition(std::get<GroundLocation>(item.location).position));
    if (p.x < -80 || p.x > W + 80 || p.y < -80 || p.y > H - HUD + 80)
        return;
    // Diablerie Loot.selected / Materials.SetRendererHighlighted. Preserve the MPQ
    // pixels and alpha; highlight the same current frame used for hit testing.
    drawSelectableSprite(groundItemSprite(item), p, highlighted, WHITE, {3.f, 1.01f});
}
std::vector<SceneView::LootLabel> SceneView::lootLabels(Vec mouse) const {
    std::vector<LootLabel> layout, visible;
    for (const auto &[id,item]:inventoryView_.items) {
        const auto *location=std::get_if<GroundLocation>(&item.location);
        if (!location || location->region!=mapView().region) continue;
        auto ground = screen(staticUnitPosition(std::get<GroundLocation>(item.location).position));
        if (ground.x < 0 || ground.x > W || ground.y < 70 || ground.y > H - HUD - 38)
            continue;
        std::string text = item.name;
        // Diablerie Item.GetTitle: only gold includes its quantity in the title.
        if (item.definition == "gld")
            text = std::to_string(item.quantity) + " " + text;
        int fontSize = 16;
        while (fontSize > 1 && painter_.measure(text, fontSize) > W - 16) --fontSize;
        float width = float(std::min(W - 8, painter_.measure(text, fontSize) + 8));
        Rectangle box{std::clamp(ground.x - width / 2, 4.f, W - width - 4), ground.y - 30, width, 22};
        bool placed = false;
        // Alternate rows above and below the drop; drawing and clicking use this same layout.
        for (int step = 0; step < 42; ++step) {
            int row = step == 0 ? 0 : (step % 2 ? -(step + 1) / 2 : step / 2);
            box.y = ground.y - 30 + row * 24;
            if (box.y < 68 || box.y + box.height > H - HUD - 36)
                continue;
            if (std::none_of(layout.begin(), layout.end(),
                             [&](const LootLabel &other) { return CheckCollisionRecs(box, other.bounds); })) {
                placed = true;
                break;
            }
        }
        if (!placed)
            continue;
        const bool plain = item.quality == ItemQuality::Normal || item.quality == ItemQuality::Superior || item.quality == ItemQuality::Inferior;
        const auto color = item.identified && item.runeword ? Color{199, 179, 119, 255} :
            plain && (item.sockets || (item.nativeFlags & 0x400000u)) ? Color{128, 128, 128, 255} : itemColor(item.quality);
        LootLabel label{item.handle(), std::move(text), box, ground, color};
        layout.push_back(label);
        if (view_.showLoot || id == inventoryView_.pickupTarget ||
            CheckCollisionPointRec(rv(mouse), box) || CheckCollisionPointRec(rv(mouse), ([&] { const auto at=screen(staticUnitPosition(location->position)); const auto *image=groundItemSprite(item);
                return image?Rectangle{at.x+image->x-5,at.y+image->y-5,float(image->texture.width+10),float(image->texture.height+10)}:Rectangle{}; }())))
            visible.push_back(std::move(label));
    }
    return visible;
}
std::optional<ItemHandle> SceneView::lootAt(Vec mouse, bool labelsOnly) const {
    for (const auto &label : lootLabels(mouse))
        if (CheckCollisionPointRec(rv(mouse), label.bounds))
            return label.item;
    if (!labelsOnly) {
        std::optional<ItemHandle> closest;
        float distance = 1000;
        for (const auto &[id,item]:inventoryView_.items) {
            const auto *location=std::get_if<GroundLocation>(&item.location);
            if(!location || location->region!=mapView().region) continue;
            const auto *image=groundItemSprite(item); if(!image) continue;
            const auto p=screen(staticUnitPosition(location->position));
            if(!CheckCollisionPointRec(rv(mouse),{p.x+image->x-5,p.y+image->y-5,float(image->texture.width+10),float(image->texture.height+10)})) continue;
            const float candidate=(p-mouse).length();
            if(candidate<distance) { closest=item.handle(); distance=candidate; }
        }
        return closest;
    }
    return std::nullopt;
}
void SceneView::drawLootLabels(Vec mouse) const {
    const auto hovered = lootAt(mouse);
    for (const auto &label : lootLabels(mouse)) {
        bool hot = CheckCollisionPointRec(rv(mouse), label.bounds) ||
            (hovered && hovered->id == label.item.id);
        bool selected = label.item.id == inventoryView_.pickupTarget;
        DrawRectangleRec(label.bounds, hot || selected ? Color{47, 47, 47, 235}
                                                         : Color{0, 0, 0, 218});
        int fontSize = 16;
        while (fontSize > 1 && painter_.measure(label.text, fontSize) > label.bounds.width - 8) --fontSize;
        painter_.inBox(label.text, label.bounds, fontSize, label.color);
    }
}
} // namespace d2x
