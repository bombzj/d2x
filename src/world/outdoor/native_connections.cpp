#include "world/outdoor/native_connections.hpp"
#include <algorithm>
#include <stdexcept>

// Native slot insertion and orth ordering: D2MOO DrlgDrlg/DrlgOutPlace/
// DrlgDrlgRoom. MIT attribution: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
NativeActLayout::ConnectionSlots &slots(const WorldCatalog &catalog, NativeActLayout &layout, int id) {
    auto [entry, fresh] = layout.connections.try_emplace(id);
    if (fresh) {
        const auto &level = catalog.level(id);
        entry->second = {level.visible, level.warps};
    }
    return entry->second;
}
int orthDirection(const NativeLevelPlacement &a, const NativeLevelPlacement &b) {
    if (a.x <= b.x) {
        if (b.x == a.x + a.width) return 2;
    } else if (a.x == b.x + b.width) return 0;
    if (a.y <= b.y) {
        if (b.y == a.y + a.height) return 3;
    } else if (a.y == b.y + b.height) return 1;
    throw std::runtime_error("Native outdoor neighbor does not share a boundary");
}
bool comesBefore(const NativeActLayout &layout, const NativeActLayout::Neighbor &a,
                 const NativeActLayout::Neighbor &b) {
    if (a.direction != b.direction) return a.direction > b.direction;
    const auto &first = layout.levels.at(a.level), &second = layout.levels.at(b.level);
    switch (b.direction) {
    case 0: return first.y > second.y;
    case 1: return first.x < second.x;
    case 2: return first.y < second.y;
    case 3: return first.x > second.x;
    }
    throw std::runtime_error("Invalid native orth direction");
}
void addOrth(const NativeActLayout &layout, std::vector<NativeActLayout::Neighbor> &list,
             NativeActLayout::Neighbor value) {
    if (list.empty()) { list.push_back(value); return; }
    // Preserve AddOrth's actual insertion walk, including its head treatment.
    if (list.size() == 1 && comesBefore(layout, list.front(), value)) {
        list.insert(list.begin(), value);
        return;
    }
    auto position = list.begin() + 1;
    while (position != list.end() && !comesBefore(layout, *position, value)) ++position;
    list.insert(position, value);
}
void touchingRange(const WorldCatalog &catalog, NativeActLayout &layout, int first, int last) {
    for (int id = first; id <= last; ++id) {
        const auto &a = layout.levels.at(id);
        for (int other = first; other <= last; ++other) {
            if (id == other) continue;
            const auto &b = layout.levels.at(other);
            const int dx = a.x >= b.x ? a.x - b.width - b.x : b.x - a.width - a.x;
            const int dy = a.y >= b.y ? a.y - b.height - b.y : b.y - a.height - a.y;
            if ((dx == 0 && dy <= -1) || (dy == 0 && dx <= -1))
                connectNativeLevels(catalog, layout, id, other);
        }
    }
}
void neighborsRange(const WorldCatalog &catalog, NativeActLayout &layout, int first, int last) {
    for (int id = first; id <= last; ++id) {
        if (catalog.level(id).generation != GenerationKind::Outdoor) continue;
        const auto &a = layout.levels.at(id);
        const auto &connections = slots(catalog, layout, id);
        auto &neighbors = layout.neighbors[id];
        for (size_t i = 0; i < connections.visible.size(); ++i) {
            const int destination = connections.visible[i];
            if (!destination || connections.warps[i] != -1) continue;
            addOrth(layout, neighbors, {destination, orthDirection(a, layout.levels.at(destination)),
                    catalog.level(destination).generation == GenerationKind::Preset});
        }
    }
}
} // namespace
void connectNativeLevels(const WorldCatalog &catalog, NativeActLayout &layout, int source,
                         int destination) {
    auto &value = slots(catalog, layout, source);
    for (size_t i = 0; i < value.visible.size(); ++i)
        if (value.visible[i] == destination) { value.warps[i] = -1; return; }
    for (size_t i = 0; i < value.visible.size(); ++i)
        if (!value.visible[i] && value.warps[i] == -1) {
            value.visible[i] = destination;
            value.warps[i] = -1;
            return;
        }
    throw std::runtime_error("Native outdoor connection slots exhausted");
}
void finalizeNativeConnections(const WorldCatalog &catalog, NativeActLayout &layout, int act) {
    if (!layout.neighbors.empty()) throw std::logic_error("Native neighbors already finalized");
    if (act == 0) neighborsRange(catalog, layout, 1, 17);
    else if (act == 1) neighborsRange(catalog, layout, 40, 46);
    else if (act == 2) {
        touchingRange(catalog, layout, 75, 83);
        neighborsRange(catalog, layout, 75, 83);
    } else if (act == 3) neighborsRange(catalog, layout, 103, 106);
    else if (act == 4) {
        touchingRange(catalog, layout, 111, 112);
        neighborsRange(catalog, layout, 111, 112);
        touchingRange(catalog, layout, 110, 111);
        touchingRange(catalog, layout, 109, 110);
    } else throw std::invalid_argument("Invalid native connection act");
}
} // namespace d2x
