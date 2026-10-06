#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
std::vector<std::pair<RegionId, size_t>> SceneView::automapLayers() const {
    std::vector<std::pair<RegionId, size_t>> layers;
    for (const auto &[index, offset] : mapView().automapRegions) {
        const auto id = mapView().regions[index].id;
        const auto seen = exploredAutomap_.layers().find(id);
        if (seen != exploredAutomap_.layers().end())
            layers.emplace_back(id, size_t(std::count(seen->second.seen.begin(), seen->second.seen.end(), uint8_t{1})));
    }
    return layers;
}
std::vector<AutomapVisibleCell> SceneView::visibleAutomapCells() const {
    const auto &scene = mapView();
    const auto viewport = worldViewport();
    std::vector<AutomapVisibleCell> visible;
    if (scene.current < 0) return visible;
    for (const auto &[slot, offset] : scene.automapRegions) {
        const auto &region = scene.regions.at(size_t(slot));
        const auto found = exploredAutomap_.layers().find(region.id);
        const AutomapLayer *seen = found != exploredAutomap_.layers().end() &&
            found->second.width == region.width && found->second.height == region.height &&
            found->second.layoutFingerprint == region.layoutFingerprint ? &found->second : nullptr;
        for (const auto &room : region.revealRooms)
            for (int y = std::max(0, room.y / 5); y <= std::min(region.height - 1, (room.y + room.height) / 5); ++y)
                for (int x = std::max(0, room.x / 5); x <= std::min(region.width - 1, (room.x + room.width) / 5); ++x) {
                    if (seen && seen->seen[size_t(y) * region.width + x]) continue;
                    const auto image = mapAssets_->terrainBounds(size_t(slot), x, y);
                    if (image.width <= 0 || image.height <= 0) continue;
                    const auto at = screen(Vec{x * 5.f, y * 5.f} + offset);
                    const float left = at.x + image.x, top = at.y + image.y;
                    if (left < viewport.x + viewport.width && top < viewport.y + viewport.height &&
                        left + image.width > viewport.x && top + image.height > viewport.y)
                        visible.push_back({slot, x, y});
                }
    }
    return visible;
}
void SceneView::revealAutomap() {
    if (exploredAutomap_.reveal(mapView(), visibleAutomapCells()))
        notice("Automap terrain changed; incompatible discovery cleared.", true);
}
bool SceneView::restoreAutomapExploration(AutomapLayers layers) {
    exploredAutomap_.restore(std::move(layers));
    if (exploredAutomap_.reveal(mapView(), visibleAutomapCells())) {
        notice("Automap terrain changed; incompatible discovery cleared.", true);
        return false;
    }
    return true;
}

void SceneView::drawMinimap(bool large) const {
    AutomapDrawView map; map.observer = mapView().observer;
    for (const auto &[index, offset] : mapView().automapRegions) {
        const auto &region = mapView().regions.at(size_t(index));
        const auto found = exploredAutomap_.layers().find(region.id);
        if (found == exploredAutomap_.layers().end()) continue;
        const auto &seen = found->second;
        if (seen.width != region.width || seen.height != region.height || seen.layoutFingerprint != region.layoutFingerprint) continue;
        if (!assets_.regionTownAutomap.at(size_t(index))[size_t(large)].empty())
            map.towns.push_back({int(region.id), assets_.regionAutomapVariants.at(size_t(index)),
                Vec{(region.width - 1) * 2.5f, (region.height - 1) * 2.5f} + offset});
        for (const auto &stamp : assets_.regionAutomap.at(size_t(index)))
            if (seen.seen.at(size_t(stamp.y) * region.width + stamp.x))
                map.stamps.push_back({Vec{stamp.x * 5.f + 2.5f, stamp.y * 5.f + 2.5f} + offset, stamp.cel});
        for (const auto &object : region.markers) {
            const int x = int(std::floor(object.position.x / 5.f)), y = int(std::floor(object.position.y / 5.f));
            if (x < 0 || y < 0 || x >= region.width || y >= region.height || !seen.seen[size_t(y) * region.width + x]) continue;
            map.markers.push_back({object.position + offset,
                object.npcClass.empty() ? assets_.automapObjectCel(object.objectClass) : assets_.automapNpcCel(object.npcClass),
                object.name, !object.npcClass.empty(), object.showName});
        }
    }
    drawAutomap(map);
}
void SceneView::drawAutomap(const AutomapDrawView &map) const {
    if (!view_.automap) return;
    const bool large = view_.automapLarge;
    const auto viewport = worldViewport();
    // Keep the map inside the visible world when a side panel is open.
    const float width = std::min(240.f, viewport.width - 24);
    const Rectangle area = large ? viewport
        : Rectangle{view_.minimapRight ? viewport.x + viewport.width - width - 12 : viewport.x + 12,
                    38, width, 175};
    const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    const float scale = large ? .1f : .05f;
    const Vec origin = project(map.observer);
    auto onMap = [&](Vec world) {
        const auto position = (project(world) - origin) * scale + center + view_.automapOffset;
        return Vec{std::round(position.x), std::round(position.y)};
    };
    const Rectangle fadedCenter{area.x + area.width * .25f, area.y + area.height * .25f,
        area.width * .5f, area.height * .5f};
    auto mapSprite = [&](const Sprite &image, Vec position, bool npc = false) {
        if (npc || view_.automapFade == AutomapFade::No) {
            sprite(&image, position, WHITE);
        } else if (view_.automapFade != AutomapFade::Center || !large) {
            sprite(&image, position, {255, 255, 255, 128});
        } else {
            const float left = position.x + image.x, top = position.y + image.y;
            auto clipped = [&](Rectangle clip, uint8_t alpha) {
                const float clipLeft = std::max(left, clip.x), clipTop = std::max(top, clip.y);
                const float clipRight = std::min(left + image.texture.width, clip.x + clip.width);
                const float clipBottom = std::min(top + image.texture.height, clip.y + clip.height);
                if (clipRight <= clipLeft || clipBottom <= clipTop) return;
                const Rectangle source{clipLeft - left, clipTop - top,
                    clipRight - clipLeft, clipBottom - clipTop};
                DrawTexturePro(image.texture, source,
                    {clipLeft, clipTop, source.width, source.height}, {0, 0}, 0, {255, 255, 255, alpha});
            };
            clipped({area.x, area.y, area.width, fadedCenter.y - area.y}, 255);
            clipped({area.x, fadedCenter.y + fadedCenter.height, area.width,
                area.y + area.height - fadedCenter.y - fadedCenter.height}, 255);
            clipped({area.x, fadedCenter.y, fadedCenter.x - area.x, fadedCenter.height}, 255);
            clipped({fadedCenter.x + fadedCenter.width, fadedCenter.y,
                area.x + area.width - fadedCenter.x - fadedCenter.width, fadedCenter.height}, 255);
            clipped(fadedCenter, 128);
        }
    };
    BeginScissorMode(int(area.x), int(area.y), int(area.width), int(area.height));
    BeginBlendMode(BLEND_ALPHA);
    for (const auto &town : map.towns)
        for (const auto &image : assets_.townAutomapSprites(town.level, town.variant, large))
            mapSprite(image, onMap(town.center));
    for (const auto &stamp : map.stamps)
        if (const auto *image = assets_.automapSprite(stamp.cel, large))
            mapSprite(*image, onMap(stamp.position));
    for (const auto &marker : map.markers)
        if (const auto *image = assets_.automapSprite(marker.cel, large))
            mapSprite(*image, onMap(marker.position), marker.npc);
    EndBlendMode();
    if (view_.automapNames) for (const auto &marker : map.markers) {
        if (!marker.showName) continue;
        const auto position = onMap(marker.position);
        painter_.label(marker.name, int(position.x - painter_.measure(marker.name, 10) / 2),
            int(position.y - 20), 10, marker.npc ? gold : WHITE);
    }
    const auto player = onMap(map.observer);
    DrawCircleV(rv(player), large ? 2.f : 1.f, WHITE);
    DrawLineV(rv(player + Vec{-5, 0}), rv(player + Vec{5, 0}), WHITE);
    DrawLineV(rv(player + Vec{0, -4}), rv(player + Vec{0, 4}), WHITE);
    EndScissorMode();
}
} // namespace d2x
