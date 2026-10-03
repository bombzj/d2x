#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
std::vector<std::pair<RegionId, size_t>> SceneView::automapLayers() const {
    std::vector<std::pair<RegionId, size_t>> layers;
    for (const auto &[index, offset] : mapView().automapRegions) {
        const auto id = mapView().regions[index].id;
        const auto seen = exploredAutomap_.find(id);
        if (seen != exploredAutomap_.end())
            layers.emplace_back(id, size_t(std::count(seen->second.begin(), seen->second.end(), uint8_t{1})));
    }
    return layers;
}
void SceneView::revealAutomap() {
    const auto &scene = mapView();
    const auto &region = scene.regions.at(size_t(scene.current));
    auto &seen = exploredAutomap_[region.id];
    const size_t cells = size_t(region.width) * region.height;
    if (seen.size() != cells) seen.assign(cells, 0);
    if (region.safe) {
        std::fill(seen.begin(), seen.end(), 1);
        return;
    }
    if (scene.hasObserverRoom) {
        for (const auto &[slot, offset] : scene.automapRegions) {
            const auto &adjacent = scene.regions[size_t(slot)];
            auto &revealed = exploredAutomap_[adjacent.id];
            if (revealed.size() != size_t(adjacent.width) * adjacent.height)
                revealed.assign(size_t(adjacent.width) * adjacent.height, 0);
            for (const auto &room : adjacent.revealRooms)
                for (int y = std::max(0, room.y / 5); y <= std::min(adjacent.height - 1, (room.y + room.height) / 5); ++y)
                    for (int x = std::max(0, room.x / 5); x <= std::min(adjacent.width - 1, (room.x + room.width) / 5); ++x)
                        revealed[size_t(y) * adjacent.width + x] = 1;
        }
    } else {
        const int x = int(std::floor(scene.observer.x / 5.f)), y = int(std::floor(scene.observer.y / 5.f));
        if (x >= 0 && y >= 0 && x < region.width && y < region.height) seen[size_t(y) * region.width + x] = 1;
    }
}

void SceneView::drawMinimap(bool large) const {
    const auto regions = mapView().automapRegions;
    const auto viewport = worldViewport();
    // Keep the map inside the visible world when a side panel is open.
    const float width = std::min(240.f, viewport.width - 24);
    const Rectangle area = large ? viewport
        : Rectangle{view_.minimapRight ? viewport.x + viewport.width - width - 12 : viewport.x + 12,
                    38, width, 175};
    const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    const float scale = large ? .1f : .05f;
    const Vec origin = project(mapView().observer);
    auto onMap = [&](Vec world) {
        const auto position = (project(world) - origin) * scale + center + view_.automapOffset;
        return Vec{std::round(position.x), std::round(position.y)};
    };
    const auto &art = assets_.automapCels[large ? 1 : 0];
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
    for (const auto &[index, offset] : regions) {
        const auto &region = mapView().regions[index];
        const auto seen = exploredAutomap_.find(region.id);
        if (seen == exploredAutomap_.end()) continue;
        for (const auto &image : assets_.regionTownAutomap[size_t(index)][large ? 1 : 0])
            mapSprite(image, onMap(Vec{(region.width - 1) * 2.5f,
                (region.height - 1) * 2.5f} + offset));
        for (const auto &stamp : assets_.regionAutomap[index]) {
            if (!seen->second[size_t(stamp.y) * region.width + stamp.x]) continue;
            auto cell = art.find(stamp.cel);
            if (cell == art.end()) continue;
            Vec position = onMap(Vec{stamp.x * 5.f + 2.5f, stamp.y * 5.f + 2.5f} + offset);
            const auto &image = cell->second;
            if (position.x + image.x + image.texture.width < area.x ||
                position.x + image.x > area.x + area.width ||
                position.y + image.y + image.texture.height < area.y ||
                position.y + image.y > area.y + area.height) continue;
            mapSprite(image, position);
        }
    }
    for (const auto &[index, offset] : regions) {
        const auto &region = mapView().regions[index];
        const auto seen = exploredAutomap_.find(region.id);
        if (seen == exploredAutomap_.end()) continue;
        auto marker = [&](int cel, Vec position, bool npc = false) {
            auto cell = art.find(cel);
            if (cell == art.end()) return;
            int x = int(std::floor(position.x / 5.f)), y = int(std::floor(position.y / 5.f));
            if (x < 0 || y < 0 || x >= region.width || y >= region.height ||
                !seen->second[size_t(y) * region.width + x]) return;
            mapSprite(cell->second, onMap(position + offset), npc);
        };
        for (const auto &object : region.markers)
            marker(object.npcClass.empty() ? assets_.automapObjectCel(object.objectClass)
                                           : assets_.automapNpcCel(object.npcClass), object.position,
                   !object.npcClass.empty());
    }
    EndBlendMode();
    for (const auto &[index, offset] : regions) {
        const auto &region = mapView().regions[index];
        const auto seen = exploredAutomap_.find(region.id);
        if (!view_.automapNames || seen == exploredAutomap_.end()) continue;
        for (const auto &object : region.markers) {
            if (!object.showName) continue;
            const int x = int(std::floor(object.position.x / 5.f)), y = int(std::floor(object.position.y / 5.f));
            if (x < 0 || y < 0 || x >= region.width || y >= region.height ||
                !seen->second[size_t(y) * region.width + x]) continue;
            const auto position = onMap(object.position + offset);
            painter_.label(object.name, int(position.x - painter_.measure(object.name, 10) / 2),
                           int(position.y - 20), 10, object.npcClass.empty() ? WHITE : gold);
        }
    }
    const auto player = onMap(mapView().observer);
    DrawCircleV(rv(player), large ? 2.f : 1.f, WHITE);
    DrawLineV(rv(player + Vec{-5, 0}), rv(player + Vec{5, 0}), WHITE);
    DrawLineV(rv(player + Vec{0, -4}), rv(player + Vec{0, 4}), WHITE);
    EndScissorMode();
}
} // namespace d2x
