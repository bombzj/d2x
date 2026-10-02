#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
std::vector<std::pair<int, Vec>> automapRegions(const GameSession &session) {
    std::vector<std::pair<int, Vec>> regions{{session.regionIndex(), {}}};
    const auto &origin = session.region().recipe;
    std::set<int> included{session.regionIndex()};
    for (size_t next = 0; next < regions.size(); ++next) {
        const auto &recipe = session.regions()[regions[next].first].recipe;
        for (int index = 0; index < int(session.regions().size()); ++index) {
            const auto &candidate = session.regions()[index];
            if (included.contains(index) ||
                std::none_of(recipe.boundaries.begin(), recipe.boundaries.end(), [&](const auto &boundary) {
                    return boundary.destination == int(candidate.definition.id);
                })) continue;
            included.insert(index);
            regions.push_back({index, {float((candidate.recipe.worldX - origin.worldX) * 5),
                                       float((candidate.recipe.worldY - origin.worldY) * 5)}});
        }
    }
    return regions;
}
} // namespace
std::vector<std::pair<RegionId, size_t>> SceneView::automapLayers() const {
    std::vector<std::pair<RegionId, size_t>> layers;
    for (const auto &[index, offset] : automapRegions(session_)) {
        const auto id = session_.regions()[index].definition.id;
        const auto seen = exploredAutomap_.find(id);
        if (seen != exploredAutomap_.end())
            layers.emplace_back(id, size_t(std::count(seen->second.begin(), seen->second.end(), uint8_t{1})));
    }
    return layers;
}
void SceneView::revealAutomap() {
    const auto &region = session_.region();
    const auto &map = region.map;
    auto &seen = exploredAutomap_[region.definition.id];
    const size_t cells = size_t(map.data.width) * map.data.height;
    if (seen.size() != cells) seen.assign(cells, 0);
    if (region.definition.safe) {
        std::fill(seen.begin(), seen.end(), 1);
        return;
    }
    const Vec player = session_.state().player.pos;
    if (const auto *observer = map.activation.room(player)) {
        // Native client room updates include adjacent outdoor levels. Compare
        // room bounds in one coordinate system; stairs/portals are not connected.
        for (const auto &[index, offset] : automapRegions(session_)) {
            const auto &adjacent = session_.regions()[index];
            const auto &data = adjacent.map.data;
            RoomBounds localObserver = *observer;
            localObserver.x -= int(offset.x);
            localObserver.y -= int(offset.y);
            auto &revealed = exploredAutomap_[adjacent.definition.id];
            if (revealed.size() != size_t(data.width) * data.height)
                revealed.assign(size_t(data.width) * data.height, 0);
            for (const auto *room : adjacent.map.activation.nearRooms(localObserver)) {
                for (int y = std::max(0, room->y / 5);
                     y <= std::min(data.height - 1, (room->y + room->height) / 5); ++y)
                    for (int x = std::max(0, room->x / 5);
                         x <= std::min(data.width - 1, (room->x + room->width) / 5); ++x)
                        revealed[size_t(y) * data.width + x] = 1;
            }
        }
    } else {
        const int x = int(std::floor(player.x / 5.f));
        const int y = int(std::floor(player.y / 5.f));
        if (x >= 0 && y >= 0 && x < map.data.width && y < map.data.height)
            seen[size_t(y) * map.data.width + x] = 1;
    }
}

void SceneView::drawMinimap(bool large) const {
    const auto regions = automapRegions(session_);
    const auto viewport = worldViewport();
    // Keep the map inside the visible world when a side panel is open.
    const float width = std::min(240.f, viewport.width - 24);
    const Rectangle area = large ? viewport
        : Rectangle{view_.minimapRight ? viewport.x + viewport.width - width - 12 : viewport.x + 12,
                    38, width, 175};
    const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    const float scale = large ? .1f : .05f;
    const Vec origin = project(session_.state().player.pos);
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
        const auto &region = session_.regions()[index];
        const auto &map = region.map;
        const auto seen = exploredAutomap_.find(region.definition.id);
        if (seen == exploredAutomap_.end()) continue;
        for (const auto &stamp : assets_.regionAutomap[index]) {
            if (!seen->second[size_t(stamp.y) * map.data.width + stamp.x]) continue;
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
        const auto &region = session_.regions()[index];
        const auto &map = region.map;
        const auto seen = exploredAutomap_.find(region.definition.id);
        if (seen == exploredAutomap_.end()) continue;
        auto marker = [&](int cel, Vec position, bool npc = false) {
            auto cell = art.find(cel);
            if (cell == art.end()) return;
            int x = int(std::floor(position.x / 5.f)), y = int(std::floor(position.y / 5.f));
            if (x < 0 || y < 0 || x >= map.data.width || y >= map.data.height ||
                !seen->second[size_t(y) * map.data.width + x]) return;
            mapSprite(cell->second, onMap(position + offset), npc);
        };
        for (const auto &object : region.objects)
            if (!object.questHidden)
                marker(object.npcClass.empty() ? assets_.automapObjectCel(object.objectClass)
                                               : assets_.automapNpcCel(object.npcClass), object.pos,
                       !object.npcClass.empty());
        for (const auto &portal : session_.portals(region.definition.id))
            marker(assets_.automapObjectCel(59), portal.position);
        if (index == session_.regionIndex())
            if (auto cainPortal = session_.cainPortalPosition())
                marker(assets_.automapObjectCel(60), *cainPortal);
    }
    EndBlendMode();
    for (const auto &[index, offset] : regions) {
        const auto &region = session_.regions()[index];
        const auto &map = region.map;
        const auto seen = exploredAutomap_.find(region.definition.id);
        if (!view_.automapNames || seen == exploredAutomap_.end()) continue;
        for (const auto &object : region.objects) {
            if (object.questHidden || object.interaction == Interaction::None ||
                (object.npcClass.empty() && object.interaction != Interaction::Stash)) continue;
            const int x = int(std::floor(object.pos.x / 5.f)), y = int(std::floor(object.pos.y / 5.f));
            if (x < 0 || y < 0 || x >= map.data.width || y >= map.data.height ||
                !seen->second[size_t(y) * map.data.width + x]) continue;
            const auto position = onMap(object.pos + offset);
            painter_.label(object.name, int(position.x - painter_.measure(object.name, 10) / 2),
                           int(position.y - 20), 10, object.npcClass.empty() ? WHITE : gold);
        }
    }
    const auto player = onMap(session_.state().player.pos);
    DrawCircleV(rv(player), large ? 2.f : 1.f, WHITE);
    DrawLineV(rv(player + Vec{-5, 0}), rv(player + Vec{5, 0}), WHITE);
    DrawLineV(rv(player + Vec{0, -4}), rv(player + Vec{0, 4}), WHITE);
    EndScissorMode();
}
} // namespace d2x
