#pragma once
#include "world/map_terrain.hpp"
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace d2x {
// D2MOO UpdatePops/TogglePopsVisibility; 1.13c D2Common RVA A6A0/A180.
// This is display state. It never changes collision or authoritative units.
class PresetPops {
    using Parent = std::tuple<int, int, int, int, int>;
    using Key = std::tuple<int, int, int, int, int, int, int, uint32_t>;
    struct State {
        int red{255}, from{255};
        double changed{};
        bool hidden{}, special{}, initialized{};
        double deadline{};
    };
    std::map<Key, State> tiles_;
    std::map<std::pair<Parent, int>, double> hiddenGroups_;
    std::vector<uint8_t> alpha_;
    static int red(const State &state, double now) {
        const auto distance = int(std::max(0.0, now - state.changed) * 510.0);
        return state.hidden ? std::max(0, state.from - distance) : std::min(255, state.from + distance);
    }
  public:
    void clear() { tiles_.clear(); hiddenGroups_.clear(); alpha_.clear(); }
    void update(const MapTerrain &terrain, Vec position, int originX, int originY, double seconds) {
        alpha_.assign(terrain.instances.size(), 255);
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
            return Parent{room.level, room.preset, room.file, room.parentX + originX, room.parentY + originY};
        };
        const auto &own = terrain.rooms[*current];
        const auto ownParent = parent(own);
        int group = 0;
        if (own.preset) for (const auto &popup : terrain.data.roofPopups)
            if (popup.parentX == own.parentX && popup.parentY == own.parentY && popup.contains(position)) {
                group = popup.group; break;
            }
        const auto selected = std::pair{ownParent, group};
        std::erase_if(hiddenGroups_, [&](const auto &entry) { return entry.first != selected; });
        const auto deadline = hiddenGroups_.try_emplace(selected, seconds + .5).first->second;
        const std::set<size_t> near(own.near.begin(), own.near.end());
        std::set<Key> retained;
        for (size_t i = 0; i < terrain.instances.size(); ++i) {
            const auto &tile = terrain.instances[i];
            if (!tile.wallArray) continue;
            const auto &room = terrain.rooms.at(tile.room);
            const auto *entry = terrain.tiles.at(size_t(tile.tile));
            const Key key{room.x + originX, room.y + originY, tile.x + originX, tile.y + originY,
                tile.type, entry->main, entry->sub, tile.flags & 0x1c000};
            retained.insert(key);
            auto &state = tiles_[key];
            bool applies = false, hidden = false;
            if (near.contains(tile.room)) for (const auto &popup : terrain.data.roofPopups) {
                if (popup.parentX != room.parentX || popup.parentY != room.parentY) continue;
                if (tile.x < popup.x - 1 || tile.y < popup.y - 1 ||
                    tile.x >= popup.x + popup.width + 1 || tile.y >= popup.y + popup.height + 1) continue;
                if (!(tile.flags & 0x200) && ((tile.flags & 0x100) || entry->main != popup.roofMain)) continue;
                applies = true;
                hidden = parent(room) == ownParent && popup.group == group;
                if (hidden) break;
            }
            if (applies && (!state.initialized || state.hidden != hidden)) {
                state.red = state.initialized ? red(state, seconds) : 255;
                state.from = state.red;
                state.changed = seconds;
                state.hidden = hidden;
                state.special = (tile.flags & 0x200) != 0;
                state.initialized = true;
                state.deadline = seconds + .5;
                if (hidden && state.red == 255) state.changed = deadline - .5;
            }
            if (state.initialized) {
                state.red = red(state, seconds);
                // Native 0x200 walls reveal immediately on entry and hide
                // after the fade-in deadline on departure.
                alpha_[i] = state.special ? uint8_t(state.hidden || seconds < state.deadline ? 255 : 0)
                    : uint8_t(state.red);
            } else if (tile.flags & 8) alpha_[i] = 0;
        }
        std::erase_if(tiles_, [&](const auto &entry) { return !retained.contains(entry.first); });
    }
    uint8_t alpha(size_t tile) const { return alpha_.at(tile); }
};
} // namespace d2x
