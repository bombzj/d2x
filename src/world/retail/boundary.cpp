#include "boundary.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <list>
#include <stdexcept>

// Independent value adaptation of D2MOO DrlgVer::CreateVertices,
// DrlgOutdoors::GetOutLinkVisFlag, OutPlace::SetOutGridLinkFlags and
// OutWild::InitAct1OutdoorLevel's cliff annotation. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
std::vector<RetailBoundaryVertex> buildRetailBoundary(const NativeActLayout &layout, int level) {
    const auto &box = layout.levels.at(level);
    if (box.width < 16 || box.height < 16)
        throw std::runtime_error("Outdoor boundary has insufficient eight-tile dimensions");
    std::list<RetailBoundaryVertex> ring{{box.x, box.y + box.height - 1},
        {box.x, box.y}, {box.x + box.width - 1, box.y},
        {box.x + box.width - 1, box.y + box.height - 1}};
    using Cursor = decltype(ring)::iterator;
    std::array<Cursor, 4> corners;
    auto cursor = ring.begin();
    for (auto &corner : corners) corner = cursor++;
    auto next = [&](Cursor at) { return ++at == ring.end() ? ring.begin() : at; };
    auto insert = [&](Cursor after, int axis, bool vertical) {
        auto value = *after;
        value.flags = 0;
        value.direction = 0;
        if (vertical) value.y = axis; else value.x = axis;
        return ring.insert(std::next(after), value);
    };
    const auto found = layout.neighbors.find(level);
    if (found != layout.neighbors.end())
        for (const auto &neighbor : found->second) {
            const auto &other = layout.levels.at(neighbor.level);
            if (neighbor.direction < 0 || neighbor.direction > 3)
                throw std::runtime_error("Invalid native boundary direction");
            auto anchor = corners[size_t(neighbor.direction)];
            const auto end = corners[size_t((neighbor.direction + 1) % 4)];
            const bool vertical = !(neighbor.direction & 1);
            const int sign = neighbor.direction == 0 || neighbor.direction == 3 ? -1 : 1;
            const int axis = vertical ? other.y : other.x;
            const int extent = (vertical ? other.height : other.width) - 1;
            const int start = sign > 0 ? axis : axis + extent;
            const int finish = sign > 0 ? axis + extent : axis;
            const int edgeStart = vertical ? anchor->y : anchor->x;
            const int edgeEnd = vertical ? end->y : end->x;
            bool overlaps = false;
            if (sign * start > sign * edgeStart) {
                if (sign * start <= sign * edgeEnd) {
                    anchor = insert(anchor, start, vertical);
                    overlaps = true;
                }
            } else if (sign * finish >= sign * edgeStart) overlaps = true;
            if (overlaps) {
                anchor->flags |= neighbor.preset ? 3u : 1u;
                if (sign * finish < sign * edgeEnd) insert(anchor, finish, vertical);
            }
        }
    for (auto &vertex : ring) {
        vertex.x = (vertex.x - box.x) / 8;
        vertex.y = (vertex.y - box.y) / 8;
    }
    auto head = ring.begin();
    cursor = head;
    do {
        const auto following = next(cursor);
        if (cursor->x == following->x && cursor->y == following->y) {
            if (following == head) head = cursor;
            cursor->flags |= following->flags;
            cursor->direction = following->direction;
            ring.erase(following);
        }
        cursor = next(cursor);
    } while (cursor != head);
    std::vector<RetailBoundaryVertex> result;
    cursor = head;
    do { result.push_back(*cursor); cursor = next(cursor); } while (cursor != head);
    if (result.size() < 4) throw std::runtime_error("Degenerate native outdoor boundary");
    return result;
}
uint32_t markRetailAct1Cliffs(int level, std::span<RetailBoundaryVertex> ring) {
    if (level == 2 || level == 3 || level == 17) return 0;
    if (ring.size() < 4) throw std::invalid_argument("Incomplete native boundary ring");
    const auto count = ring.size();
    auto next = [&](size_t i) { return (i + 1) % count; };
    auto stops = [&](size_t i) {
        const auto &a = ring[i], &b = ring[next(i)];
        return !(a.y >= b.y && a.x <= b.x && !(a.flags & 1)) || (b.flags & 1);
    };
    auto special = [&](size_t i) {
        const auto &a = ring[i], &b = ring[next(i)], &c = ring[next(next(i))];
        return !(a.flags & 1) && !(b.flags & 1) &&
               ((a.x < b.x && b.y < c.y) || (a.y > b.y && b.x < c.x));
    };
    uint32_t flags = 0;
    size_t current = 0, previous = count - 1;
    bool reachedHead = false;
    do {
        const auto &a = ring[current], &b = ring[next(current)], &p = ring[previous];
        if (!(a.flags & 1) && !(p.flags & 1) &&
            ((a.x < b.x && p.y > a.y) || (a.y > b.y && p.x > a.x))) {
            const auto first = current;
            std::optional<size_t> last;
            do {
                if (!current) reachedHead = true;
                if (stops(current)) break;
                if (special(current)) last = current;
                current = next(current);
            } while (current != first);
            if (last) {
                for (auto i = first; i != *last; i = next(i)) ring[i].direction = 1;
                ring[*last].direction = 1;
                flags |= 0x20;
            }
        }
        previous = current;
        current = next(current);
    } while (!reachedHead && current);
    return flags;
}
void markRetailBoundaryLinks(const NativeActLayout &layout, int level,
                             std::span<const RetailBoundaryVertex> ring, RetailOutdoorGrid &grid) {
    const auto &box = layout.levels.at(level);
    if (ring.size() < 4 || grid.width() != box.width / 8 || grid.height() != box.height / 8)
        throw std::invalid_argument("Boundary and native outdoor grid disagree");
    auto visible = [&](const RetailBoundaryVertex &vertex) -> uint32_t {
        int direction;
        if (vertex.x == 0) direction = vertex.y == 0 ? 1 : 0;
        else if (vertex.y == 0) direction = vertex.x == grid.width() - 1 ? 2 : 1;
        else if (vertex.x == grid.width() - 1) direction = vertex.y == grid.height() - 1 ? 3 : 2;
        else if (vertex.y == grid.height() - 1) direction = 3;
        else return 0;
        constexpr std::array<std::pair<int, int>, 4> offsets{{{-4,4}, {4,-4}, {12,4}, {4,12}}};
        const auto [dx, dy] = offsets[size_t(direction)];
        const int x = box.x + dx + 8 * vertex.x, y = box.y + dy + 8 * vertex.y;
        const auto found = layout.neighbors.find(level);
        if (found == layout.neighbors.end()) return 0;
        for (const auto &neighbor : found->second) {
            const auto &other = layout.levels.at(neighbor.level);
            if (neighbor.direction != direction || x < other.x || y < other.y ||
                x >= other.x + other.width || y >= other.y + other.height) continue;
            const auto &slots = layout.connections.at(level).visible;
            for (size_t slot = 0; slot < slots.size(); ++slot)
                if (slots[slot] == neighbor.level) return 1u << (slot + 4);
            return 0;
        }
        return 0;
    };
    for (size_t i = 0; i < ring.size(); ++i) {
        const auto &a = ring[i], &b = ring[(i + 1) % ring.size()];
        if (!(a.flags & 1)) continue;
        if (a.x != b.x && a.y != b.y) throw std::runtime_error("Non-orthogonal native boundary");
        const uint32_t links = visible(a), flags = a.direction ? 3u : 1u;
        // Native SetOutGridLinkFlags includes both endpoints.
        for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y)
            for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x) {
                auto &cell = grid.cell(x, y);
                cell.links |= links;
                cell.flags |= flags;
            }
    }
}
} // namespace d2x
