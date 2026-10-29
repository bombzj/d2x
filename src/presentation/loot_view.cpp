#include "presentation/world/palette_blend_view.hpp"
#include "core/text.hpp"
#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::advanceGroundAnimations(float dt) {
    if (groundGameGeneration_ != inventoryView_.gameGeneration || groundAreaGeneration_ != inventoryView_.areaGeneration) {
        groundGameGeneration_ = inventoryView_.gameGeneration; groundAreaGeneration_ = inventoryView_.areaGeneration;
        groundAnimations_.clear();
    }
    std::erase_if(groundAnimations_, [&](const auto &entry) {
        const auto *item = inventoryView_.item(entry.first);
        const auto *location = item ? std::get_if<GroundLocation>(&item->location) : nullptr;
        return !location || location->region != mapView().region || !item->groundAnimationRevision;
    });
    for (auto &[id, animation] : groundAnimations_) {
        (void)id;
        animation.age = std::min(4.f, animation.age + std::max(0.f, dt));
    }
    for (const auto &[id, item] : inventoryView_.items) {
        const auto *location = std::get_if<GroundLocation>(&item.location);
        if (!location || location->region != mapView().region || !item.groundAnimationRevision) continue;
        auto &animation = groundAnimations_[id];
        if (animation.revision != item.groundAnimationRevision)
            animation = {item.groundAnimationRevision, std::max(0.f, item.groundAnimationAge)};
        if (!animation.flipSoundConsumed || !animation.landSoundConsumed) {
            const auto *art = assets_.groundItemAnimation(item);
            if (!art || art->count <= 0) continue;
            const auto at = screen(staticUnitPosition(location->position));
            const auto viewport = worldViewport();
            const bool audible = CheckCollisionPointRec(rv(at),viewport);
            if (!animation.flipSoundConsumed)
                animation.flipSoundConsumed = assets_.sceneAudio.itemDrop(
                    {item.definition,item.quality,item.specialRow,animation.age,audible,ItemDropSoundEvent::Kind::Flip});
            if (!animation.landSoundConsumed)
                animation.landSoundConsumed = assets_.sceneAudio.itemDrop(
                    {item.definition,item.quality,item.specialRow,animation.age,audible});
        }
    }
}
const Sprite *SceneView::groundItemSprite(const InventoryItemView &item) const {
    const auto *art = assets_.groundItemAnimation(item);
    if (!art || art->count <= 0) return nullptr;
    const auto animation = groundAnimations_.find(item.id);
    // Preserve the old single-player 25 Hz flippy and final-frame hold. Only
    // its trigger now comes from the server item event rather than ItemChange.
    const int frame=animation==groundAnimations_.end() || animation->second.age>=4.f?art->count-1:
        std::min(art->count-1,int(animation->second.age*25));
    return art->frame(0,frame);
}
void SceneView::drawGroundItem(EntityId id, bool highlighted) const {
    const auto *item = inventoryView_.item(id);
    if (item) drawGroundItem(*item, highlighted);
}
void SceneView::drawGroundItem(const InventoryItemView &item, bool highlighted,
                               const PaletteBlendView *lighting, Color light) const {
    auto p = screen(staticUnitPosition(std::get<GroundLocation>(item.location).position));
    if (p.x < -80 || p.x > W + 80 || p.y < -80 || p.y > H - HUD + 80)
        return;
    const auto *image = groundItemSprite(item);
    if (!image) return;
    if (lighting) {
        lighting->drawLit(image, p, {&light, 1}, WHITE, highlighted ? Vector2{1.7f, 1} : Vector2{1, 1});
        return;
    }
    // D2Client 1.13c RVA 6CF9B: ground items use CelDraw 5, or 7 when
    // selected. Ethereal inventory TRANS50 is not applied to ground flippy.
    const int act = mapView().palette;
    const auto *blend = act == 0 ? paletteBlend_.get() : actPaletteBlends_.at(size_t(act)).get();
    if (highlighted && blend)
        blend->drawHighlight(image, {p.x + image->x, p.y + image->y,
            float(image->texture.width), float(image->texture.height)});
    else sprite(image, p);
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
        while (fontSize > 1 && painter_.measure(stripD2ColorCodes(text), fontSize) > W - 16) --fontSize;
        float width = float(std::min(W - 8, painter_.measure(stripD2ColorCodes(text), fontSize) + 8));
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
        const auto tone = item.tooltip.empty() ? ItemTextTone::Name : item.tooltip.front().tone;
        LootLabel label{item.handle(), std::move(text), box, ground, itemTextFontIndex(item.quality, tone)};
        layout.push_back(label);
        if (view_.showLoot || view_.bhAlwaysShowLoot || id == inventoryView_.pickupTarget ||
            CheckCollisionPointRec(rv(mouse), box) ||
            spriteHit(groundItemSprite(item),ground,mouse,false))
            visible.push_back(std::move(label));
    }
    return visible;
}
std::optional<ItemHandle> SceneView::lootAt(Vec mouse, bool labelsOnly) const {
    for (const auto &label : lootLabels(mouse))
        if (CheckCollisionPointRec(rv(mouse), label.bounds))
            if (const auto *item = inventoryView_.item(label.item.id); item && item->groundPickupAllowed)
                return label.item;
    if (!labelsOnly) {
        std::optional<ItemHandle> closest;
        float distance = 1000;
        for (const auto &[id,item]:inventoryView_.items) {
            const auto *location=std::get_if<GroundLocation>(&item.location);
            if(!location || location->region!=mapView().region || !item.groundPickupAllowed) continue;
            const auto *image=groundItemSprite(item); if(!image) continue;
            const auto p=screen(staticUnitPosition(location->position));
            if(!spriteHit(image,p,mouse,false)) continue;
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
        while (fontSize > 1 && painter_.measure(stripD2ColorCodes(label.text), fontSize) > label.bounds.width - 8)
            --fontSize;
        drawColoredItemText(label.text, label.fontIndex, label.bounds, fontSize);
    }
}
} // namespace d2x
