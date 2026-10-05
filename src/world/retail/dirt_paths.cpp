#include "dirt_paths.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

// D2MOO DrlgOutdoors::SpawnAct1DirtPaths/CalculatePathCoordinates,
// sub_6FD7F5B0/F810 and DrlgOutPlace::sub_6FD80750. Native bounded
// depth-first cost search, including ties and direction order; not Grid::path.
// MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
constexpr std::array<int, 4> dx{{1,0,-1,0}}, dy{{0,1,0,-1}};
constexpr std::array<std::array<int, 4>, 4> turns{{{0,1,2,3},{0,1,1,1},{3,2,1,2},{0,3,2,1}}};
int toward(RetailPathPoint a, RetailPathPoint b) {
    // PathMisc::sub_6FDAB610 and the first column of its native 25-entry table.
    int x = b.x - a.x, y = b.y - a.y;
    const int ax = std::abs(x), ay = std::abs(y);
    int index;
    if (ax < 2 * ay) {
        if (ay >= 2 * ax) {
            if (x < 0) {
                if (y < -1) index = 5;
                else index = std::min(y, 2) + 7;
                constexpr std::array<int, 25> directions{{5,4,4,4,3,6,5,4,3,2,6,6,6,2,2,6,7,0,1,2,7,0,0,0,1}};
                return directions.at(size_t(index)) / 2;
            }
            x &= 1;
        }
    } else y = y >= 0 ? y & 1 : -1;
    x = std::clamp(x, -2, 2);
    index = y < -1 ? 5 * x + 10 : std::min(y, 2) + 5 * x + 12;
    constexpr std::array<int, 25> directions{{5,4,4,4,3,6,5,4,3,2,6,6,6,2,2,6,7,0,1,2,7,0,0,0,1}};
    return directions.at(size_t(index)) / 2;
}
int distance(RetailPathPoint a, RetailPathPoint b) {
    const int x = std::abs(a.x - b.x), y = std::abs(a.y - b.y);
    return std::min(x, y) + 2 * std::max(x, y);
}
std::vector<RetailPathPoint> route(const RetailOutdoorGrid &grid, RetailPathPoint from, RetailPathPoint to) {
    if (std::abs(from.x - to.x) + std::abs(from.y - to.y) < 2) return {from, to};
    struct Node {
        RetailPathPoint point;
        int cost{}, tried{}, table{}, offset{}, direction{}, parent{-1}, child{-1};
    };
    std::array<Node, 900> nodes;
    const int heuristic = distance(from, to);
    const int limit = heuristic + heuristic / 2 + 35;
    for (int threshold = heuristic + heuristic / 2; threshold < limit; threshold += 5) {
        nodes[0] = {from, 0, -1, 0, 0, toward(from, to)};
        int current = 0, allocated = 1;
        auto advance = [&] {
            while (current >= 0) {
                auto &node = nodes[size_t(current)];
                // Native advances its table pointer before discarding an
                // exhausted node. That final value is never observed.
                if (node.tried == 2) { ++node.tried; current = node.parent; continue; }
                if (node.tried < 4) {
                    if (++node.offset >= 4) throw std::runtime_error("Native dirt path turn table exhausted");
                    node.direction = (node.direction + turns[size_t(node.table)][size_t(node.offset)]) & 3;
                }
                ++node.tried;
                return;
            }
        };
        while (current >= 0) {
            const auto &node = nodes[size_t(current)];
            if (node.point.x == to.x && node.point.y == to.y) {
                std::vector<RetailPathPoint> result;
                for (int i = current; i >= 0; i = nodes[size_t(i)].parent) result.push_back(nodes[size_t(i)].point);
                return result;
            }
            const int direction = node.direction;
            const RetailPathPoint next{node.point.x + dx[size_t(direction)], node.point.y + dy[size_t(direction)]};
            bool allowed = next.x == to.x && next.y == to.y;
            if (!allowed && grid.contains(next.x, next.y) && !(grid.cell(next.x, next.y).flags & 0x200)) {
                allowed = true;
                for (int i = current; i >= 0; i = nodes[size_t(i)].parent)
                    if (nodes[size_t(i)].point.x == next.x && nodes[size_t(i)].point.y == next.y) { allowed = false; break; }
            }
            const int cost = node.cost + 2;
            if (!allowed || cost + distance(next, to) > threshold) { advance(); continue; }
            int child = node.child;
            if (child < 0) {
                if (++allocated >= int(nodes.size())) return {};
                child = allocated;
                nodes[size_t(current)].child = child;
                nodes[size_t(child)] = {};
            }
            const int heading = toward(next, to);
            auto &destination = nodes[size_t(child)];
            const int retainedChild = destination.child;
            const int table = (direction - heading) & 3;
            destination = {next, cost, 0, table, 0, (heading + turns[size_t(table)][0]) & 3, current, retainedChild};
            current = child;
        }
    }
    return {};
}
struct Endpoint { RetailPathPoint point; int direction{4}; };
RetailPathPoint approach(const NativeLevelPlacement &box, Endpoint endpoint) {
    auto point = endpoint.point;
    switch (endpoint.direction) {
    case 0: point.x = box.x + 8 * ((point.x - box.x) / 8) + 11; break;
    case 1: point.y = box.y + 8 * ((point.y - box.y) / 8) + 11; break;
    case 2: point.x = box.x + 8 * ((point.x - box.x) / 8) - 5; break;
    case 3: point.y = box.y + 8 * ((point.y - box.y) / 8) - 5; break;
    }
    return point;
}
} // namespace
RetailDirtPaths buildRetailDirtPaths(const NativeActLayout &layout, int level, uint32_t flags,
                                   RetailOutdoorGrid &grid) {
    const auto &box = layout.levels.at(level);
    std::vector<Endpoint> endpoints;
    if (const auto found = layout.neighbors.find(level); found != layout.neighbors.end())
        for (const auto &neighbor : found->second) {
            const auto &other = layout.levels.at(neighbor.level);
            if (neighbor.level == 1) {
                constexpr std::array<std::pair<int, int>, 4> offsets{{{59,19},{29,35},{4,22},{29,3}}};
                const auto [x, y] = offsets.at(size_t(neighbor.direction));
                endpoints.push_back({{other.x + x, other.y + y}, neighbor.direction});
            } else if (neighbor.level == 26) endpoints.push_back({{other.x + 27, other.y + 13}, 1});
        }
    // Native scan is column first, unlike most other grid phases.
    for (int x = 0; x < grid.width(); ++x)
        for (int y = 0; y < grid.height(); ++y) {
            const auto &cell = grid.cell(x, y);
            const int file = int((cell.flags >> 16) & 15);
            int direction = 4;
            if (cell.preset >= 4 && cell.preset <= 7 && file == 3) direction = (cell.preset - 1) & 3;
            else if (cell.preset == 24) direction = 1;
            else if (cell.preset == 25) direction = 0;
            else if (cell.preset == 28 && file == 1 && x == grid.width() - 2) direction = 2;
            else if (cell.preset == 51 || cell.preset == 52) direction = file != 0;
            if (direction != 4) endpoints.push_back({{box.x + 8*x + 3, box.y + 8*y + 3}, direction});
        }
    if (endpoints.size() > 6) throw std::runtime_error("Native dirt path endpoint capacity exceeded");
    if (endpoints.empty()) return {};
    std::optional<RetailPathPoint> bridge;
    if (flags & 0x10) {
        const int x = grid.width() / 2 - 1;
        // Preserve the native width bound even for rectangular wilderness.
        for (int y = 1; y < grid.width() - 1; ++y)
            if (grid.contains(x, y) && grid.cell(x, y).preset == 28 &&
                ((grid.cell(x, y).flags >> 16) & 15) == 1) {
                bridge = RetailPathPoint{box.x + 8*x + 3, box.y + 8*y + 3}; break;
            }
    }
    RetailPathPoint center;
    if (!bridge) {
        if (endpoints.size() == 1) center = {grid.width()/2, grid.height()/2};
        else {
            for (const auto &endpoint : endpoints) { center.x += endpoint.point.x - box.x; center.y += endpoint.point.y - box.y; }
            center.x /= 8 * int(endpoints.size()); center.y /= 8 * int(endpoints.size());
        }
        constexpr std::array<int, 4> sx{{-1,0,0,1}}, sy{{0,1,-1,0}};
        RetailPathPoint candidate;
        bool found = false;
        for (int radius = 0; radius < 8 && !found; ++radius)
            for (size_t i = 0; i < sx.size(); ++i) {
                candidate = {center.x + radius*sx[i], center.y + radius*sy[i]};
                if (grid.contains(candidate.x, candidate.y) && !(grid.cell(candidate.x, candidate.y).flags & 0x1B81)) { found = true; break; }
            }
        center = {box.x + 8*candidate.x + 3, box.y + 8*candidate.y + 3};
    }
    RetailDirtPaths result;
    for (const auto &endpoint : endpoints) {
        Endpoint destination{center, 4};
        if (bridge) {
            destination = {*bridge, endpoint.point.x <= bridge->x ? 2 : 0};
            if (destination.direction == 0) destination.point.x += 8;
        }
        const auto start = approach(box, endpoint), finish = approach(box, destination);
        auto points = route(grid, {(start.x-box.x)/8, (start.y-box.y)/8},
                                 {(finish.x-box.x)/8, (finish.y-box.y)/8});
        if (points.empty()) continue;
        for (const auto &point : points) if (grid.contains(point.x, point.y)) grid.cell(point.x, point.y).flags |= 0x80;
        int direction = int(grid.random().next() & 3);
        points.front() = finish;
        for (size_t i = 1; i + 1 < points.size(); ++i) {
            const int x = (int(grid.random().next() & 1) + 2) * dx[size_t(direction)];
            const int y = (int(grid.random().next() & 1) + 2) * dy[size_t(direction)];
            direction = (direction + 1) & 3;
            points[i] = {box.x + 8*points[i].x + x + 3, box.y + 8*points[i].y + y + 3};
        }
        points.back() = start;
        points.push_back(endpoint.point);
        if (destination.direction != 4) points.insert(points.begin(), destination.point);
        result.push_back(std::move(points));
    }
    return result;
}
} // namespace d2x
