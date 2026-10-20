#include "tile_visibility.hpp"
#include "world/map_terrain.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void TileVisibility::clear() { tiles_.clear(); hiddenGroups_.clear(); draw_.clear(); visit_ = 0; }
void TileVisibility::transition(State &state, int from, int to, int64_t deadline) {
    state.from = from; state.to = to; state.deadline = deadline; state.flags |= 2;
}
void TileVisibility::advance(State &state, int64_t now) {
    // Client 9EA10: signed division truncates toward zero; do not replace this
    // with a fresh interpolation starting at the current red value.
    if (state.flags & 2) {
        state.red = now >= state.deadline ? state.to :
            state.from + int((state.to - state.from) * (now - state.deadline + 500) / 500);
        if (now >= state.deadline) state.flags &= ~2;
    }
    if ((state.flags & 4) && now >= state.deadline) { state.flags &= ~4; state.hidden = true; }
}
void TileVisibility::update(const MapTerrain &terrain, Vec position, int originX, int originY, double seconds) {
    const auto now = int64_t(std::max(0.0, seconds) * 1000.0);
    draw_.assign(terrain.instances.size(), {});
    std::optional<size_t> current;
    for (size_t i = 0; i < terrain.rooms.size(); ++i) {
        const auto &room = terrain.rooms[i];
        if (position.x >= room.x * 5 && position.y >= room.y * 5 &&
            position.x < (room.x + room.width) * 5 && position.y < (room.y + room.height) * 5) {
            current = i; break;
        }
    }
    if (!current) return;
    const auto parent = [&](const MapTerrain::PreparedRoom &room) {
        return Parent{room.level,room.preset,room.file,room.parentX + originX,room.parentY + originY};
    };
    const auto logical = [](const MapTerrain::PreparedRoom &room, int x, int y)
        -> const MapTerrain::PreparedRoom::LogicalRegion * {
        if (room.logicalRegions.empty()) return nullptr;
        if (room.singleLogicalRegion) return &room.logicalRegions.front();
        x -= room.x; y -= room.y;
        if (x < 0 || y < 0 || x > room.width || y > room.height) return nullptr;
        return &room.logicalRegions.at(room.logicalCells.at(size_t(y) * (room.width + 1) + x));
    };
    const auto &own = terrain.rooms[*current];
    const int playerX = int(std::floor(position.x / 5)), playerY = int(std::floor(position.y / 5));
    const auto *observerRegion = logical(own, playerX, playerY);
    const int observerIndex = observerRegion ? observerRegion->index : -1;
    const auto ownParent = parent(own);
    int group = 0;
    if (own.preset) for (const auto &popup : terrain.data.roofPopups)
        if (popup.parentX == own.parentX && popup.parentY == own.parentY && popup.contains(position)) {
            group = popup.group; break;
        }
    const auto selected = std::pair{ownParent,group};
    std::erase_if(hiddenGroups_, [&](const auto &entry) { return entry.first != selected; });
    const auto deadline = hiddenGroups_.try_emplace(selected, now + 500).first->second;
    if (++visit_ == 0) {
        for (auto &[key, state] : tiles_) { (void)key; state.visit = 0; }
        ++visit_;
    }
    for (size_t i = 0; i < terrain.instances.size(); ++i) {
        const auto &tile = terrain.instances[i];
        if (!tile.wallArray || tile.removed) continue;
        const auto &room = terrain.rooms.at(tile.room);
        const auto *entry = terrain.tiles.at(size_t(tile.tile));
        const Key key{room.x + originX,room.y + originY,tile.x + originX,tile.y + originY,
            tile.type,entry->main,entry->sub,tile.flags & 0x1c000};
        const auto [found,fresh] = tiles_.try_emplace(key);
        auto &state = found->second;
        state.visit = visit_;
        if (fresh) state.hidden = (tile.flags & 8) != 0;
        // Common TogglePopsVisibility precedes the client foreground-wall
        // predicate. Both write the same transition fields in the original.
        bool applies = false, hidden = false;
        if (std::find(own.near.begin(), own.near.end(), tile.room) != own.near.end())
        for (const auto &popup : terrain.data.roofPopups) {
            if (popup.parentX != room.parentX || popup.parentY != room.parentY) continue;
            if (tile.x < popup.x - 1 || tile.y < popup.y - 1 ||
                tile.x >= popup.x + popup.width + 1 || tile.y >= popup.y + popup.height + 1) continue;
            if (!(tile.flags & 0x200) && ((tile.flags & 0x100) || entry->main != popup.roofMain)) continue;
            applies = true; hidden = parent(room) == ownParent && popup.group == group;
            if (hidden) break;
        }
        if (applies && state.popHidden != hidden) {
            state.popHidden = hidden;
            if (tile.flags & 0x200) {
                if (hidden) { state.flags &= ~4; state.hidden = false; }
                else { state.flags |= 4; state.deadline = now + 500; }
            } else if (hidden) {
                transition(state,255,0,state.red == 255 ? deadline : now + 500 + 500 * (state.red - 255) / 255);
            } else transition(state,0,255,now + 500 - 500 * state.red / 255);
        }
        // Client 79F20 default branch (E24F4 = 1): a wall's coordinate list,
        // not a screen rectangle or a map-specific tile type test.
        const auto *region = room.singleLogicalRegion ? nullptr : logical(room,tile.x,tile.y);
        const bool front = region && region->index != observerIndex &&
            (playerX < region->left || playerY < region->top);
        if (!(tile.flags & 4)) {
            if (front && !(state.flags & 1)) {
                transition(state,255,128,now + 500 + 500 * (state.red - 255) / 255);
                state.flags |= 1;
            } else if (!front && (state.flags & 1)) {
                transition(state,128,255,now + 500 + 500 * (128 - state.red) / 255);
                state.flags &= ~1;
            }
        }
        advance(state,now);
        draw_[i] = {state.hidden || (tile.flags & 0x400) ? uint8_t{0} : uint8_t(state.red),
                    (state.flags & 1) != 0};
    }
    std::erase_if(tiles_, [&](const auto &entry) { return entry.second.visit != visit_; });
}
} // namespace d2x
