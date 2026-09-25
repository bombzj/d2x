#include "scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
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
    bool inRoom = false;
    // Original automap reveal is room based. The map assembler supplies the
    // same room bounds used for population activation, including outdoor rooms.
    for (const auto &room : map.rooms) {
        if (player.x < room.x || player.y < room.y ||
            player.x >= room.x + room.width || player.y >= room.y + room.height) continue;
        inRoom = true;
        for (int y = std::max(0, room.y / 5);
             y < std::min(map.data.height, (room.y + room.height + 4) / 5); ++y)
            for (int x = std::max(0, room.x / 5);
                 x < std::min(map.data.width, (room.x + room.width + 4) / 5); ++x)
                seen[size_t(y) * map.data.width + x] = 1;
    }
    if (!inRoom) {
        const int x = int(std::floor(player.x / 5.f));
        const int y = int(std::floor(player.y / 5.f));
        if (x >= 0 && y >= 0 && x < map.data.width && y < map.data.height)
            seen[size_t(y) * map.data.width + x] = 1;
    }
}

void SceneView::drawMinimap(bool large) const {
    const auto &region = session_.region();
    const auto &map = region.map;
    const auto seen = exploredAutomap_.find(region.definition.id);
    if (seen == exploredAutomap_.end()) return;
    const auto viewport = worldViewport();
    // Keep the map inside the visible world when a side panel is open.
    const float width = std::min(240.f, viewport.width - 24);
    const Rectangle area = large ? viewport
        : Rectangle{view_.minimapRight ? viewport.x + viewport.width - width - 12 : viewport.x + 12,
                    38, width, 175};
    const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    // One original automap cel per 5x5 world subtile. The two MPQ DC6 files
    // contain the matching 16/8-pixel and 8/4-pixel isometric projections.
    const float scale = large ? .2f : .1f;
    const Vec origin = project(session_.state().player.pos);
    auto onMap = [&](Vec world) { return (project(world) - origin) * scale + center; };
    const auto &art = assets_.automapCels[large ? 1 : 0];
    BeginScissorMode(int(area.x), int(area.y), int(area.width), int(area.height));
    BeginBlendMode(BLEND_ADDITIVE);
    for (const auto &stamp : assets_.regionAutomap[session_.regionIndex()]) {
        if (!seen->second[size_t(stamp.y) * map.data.width + stamp.x]) continue;
        auto cell = art.find(stamp.cel);
        if (cell == art.end()) continue;
        Vec p = onMap({stamp.x * 5.f, stamp.y * 5.f});
        if (p.x < area.x - 32 || p.x > area.x + area.width + 32 ||
            p.y < area.y - 32 || p.y > area.y + area.height + 32) continue;
        sprite(&cell->second, p, {255, 255, 255, uint8_t(large ? 225 : 245)});
    }
    auto marker = [&](int cel, Vec position) {
        auto cell = art.find(cel);
        if (cell == art.end()) return;
        int x = int(position.x / 5.f), y = int(position.y / 5.f);
        if (x < 0 || y < 0 || x >= map.data.width || y >= map.data.height ||
            !seen->second[size_t(y) * map.data.width + x]) return;
        sprite(&cell->second, onMap(position), WHITE);
    };
    for (const auto &object : region.objects)
        if (!object.questHidden && object.npcClass.empty())
            marker(assets_.automapObjectCel(object.objectClass), object.pos);
    const auto &portal = session_.state().portal;
    if (portal.active) {
        if (portal.field == region.definition.id)
            marker(assets_.automapObjectCel(59), portal.fieldPosition);
        if (region.definition.id == RegionId::Encampment)
            marker(assets_.automapObjectCel(59), portal.townPosition);
    }
    if (auto cainPortal = session_.cainPortalPosition())
        marker(assets_.automapObjectCel(60), *cainPortal);
    EndBlendMode();
    for (const auto &object : region.objects) {
        if (object.questHidden || (object.npcClass.empty() && object.interaction != Interaction::Stash))
            continue;
        const int x = int(object.pos.x / 5.f), y = int(object.pos.y / 5.f);
        if (x < 0 || y < 0 || x >= map.data.width || y >= map.data.height ||
            !seen->second[size_t(y) * map.data.width + x]) continue;
        const auto p = onMap(object.pos);
        // NPC IDs belong to MonStats, never the Objects.txt automap namespace.
        // Their native marker has not been identified; do not substitute a prop cel.
        painter_.label(object.name, int(p.x - painter_.measure(object.name, 10) / 2),
                       int(p.y - 20), 10, object.npcClass.empty() ? WHITE : gold);
    }
    DrawCircleV(rv(center), large ? 2.f : 1.f, WHITE);
    DrawLineV(rv(center + Vec{-5, 0}), rv(center + Vec{5, 0}), WHITE);
    DrawLineV(rv(center + Vec{0, -4}), rv(center + Vec{0, 4}), WHITE);
    EndScissorMode();
}
} // namespace d2x
