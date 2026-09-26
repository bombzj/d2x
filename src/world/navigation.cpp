#include "navigation.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>
namespace d2x {
bool Grid::segment(Vec a, Vec b) const {
    if (!walkable(a) || !walkable(b)) return false;
    int column = int(std::floor(a.x)), row = int(std::floor(a.y));
    const int endColumn = int(std::floor(b.x)), endRow = int(std::floor(b.y));
    const double deltaX = double(b.x) - a.x, deltaY = double(b.y) - a.y;
    const int stepX = deltaX > 0 ? 1 : deltaX < 0 ? -1 : 0;
    const int stepY = deltaY > 0 ? 1 : deltaY < 0 ? -1 : 0;
    const double infinity = std::numeric_limits<double>::infinity();
    const double intervalX = stepX ? 1 / std::abs(deltaX) : infinity;
    const double intervalY = stepY ? 1 / std::abs(deltaY) : infinity;
    double crossingX = stepX ? (stepX > 0 ? column + 1.0 - a.x : a.x - column) * intervalX : infinity;
    double crossingY = stepY ? (stepY > 0 ? row + 1.0 - a.y : a.y - row) * intervalY : infinity;
    while (column != endColumn || row != endRow) {
        if (column == endColumn || (row != endRow && crossingY < crossingX)) {
            row += stepY;
            crossingY += intervalY;
        } else if (row == endRow || crossingX < crossingY) {
            column += stepX;
            crossingX += intervalX;
        } else {
            if (!walkable(column + stepX, row) || !walkable(column, row + stepY)) return false;
            column += stepX;
            row += stepY;
            crossingX += intervalX;
            crossingY += intervalY;
        }
        if (!walkable(column, row)) return false;
    }
    return true;
}
bool Grid::lightSegment(Vec a, Vec b) const {
    auto delta = b - a;
    int steps = std::max(1, int(delta.length() * 5));
    const int targetX = int(std::floor(b.x)), targetY = int(std::floor(b.y));
    // The target cell is allowed to receive light even if its own wall blocks
    // rays travelling farther. This keeps the visible face of a wall lit.
    for (int i = 0; i < steps; ++i) {
        auto p = a + delta * (float(i) / steps);
        int x = int(std::floor(p.x)), y = int(std::floor(p.y));
        if (x == targetX && y == targetY)
            return true;
        if (x < 0 || y < 0 || x >= width || y >= height || lightBlocked[y * width + x])
            return false;
    }
    return true;
}
Bytes Grid::reachableFrom(Vec origin) const {
    Bytes reachable(blocked.size());
    if (!walkable(origin)) return reachable;
    std::vector<int> pending{int(origin.y) * width + int(origin.x)};
    reachable[size_t(pending.front())] = 1;
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const int cell = pending[cursor];
        for (const auto offset : {std::pair{0, 1}, std::pair{1, 0}, std::pair{0, -1}, std::pair{-1, 0}}) {
            const int column = cell % width + offset.first;
            const int row = cell / width + offset.second;
            if (!walkable(column, row)) continue;
            const int next = row * width + column;
            if (reachable[size_t(next)]) continue;
            reachable[size_t(next)] = 1;
            pending.push_back(next);
        }
    }
    return reachable;
}
Vec Grid::nearest(Vec p) const {
    int px = std::clamp(int(p.x), 0, std::max(0, width - 1)),
        py = std::clamp(int(p.y), 0, std::max(0, height - 1));
    for (int radius = 0; radius < std::max(width, height); radius++)
        for (int y = py - radius; y <= py + radius; y++)
            for (int x = px - radius; x <= px + radius; x++)
                if ((std::abs(x - px) == radius || std::abs(y - py) == radius) && walkable(x, y))
                    return {x + .5f, y + .5f};
    return p;
}
Vec Grid::inspectionArrival() const {
    Bytes visited(blocked.size());
    std::vector<int> component;
    size_t largest = 0;
    Vec result{};
    for (int start = 0; start < int(blocked.size()); ++start) {
        if (blocked[start] || visited[start])
            continue;
        component.clear();
        component.push_back(start);
        visited[start] = 1;
        float best = std::numeric_limits<float>::infinity();
        Vec point{};
        for (size_t cursor = 0; cursor < component.size(); ++cursor) {
            int index = component[cursor], x = index % width, y = index / width;
            bool interior = true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    interior &= walkable(x + dx, y + dy);
            float dx = x + .5f - width * .5f, dy = y + .5f - height * .5f;
            float score = dx * dx + dy * dy + (interior ? 0.f : float(width * width + height * height));
            if (score < best) {
                best = score;
                point = {x + .5f, y + .5f};
            }
            for (const auto &[ox, oy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                if (!walkable(x + ox, y + oy))
                    continue;
                int next = (y + oy) * width + x + ox;
                if (!visited[next]) {
                    visited[next] = 1;
                    component.push_back(next);
                }
            }
        }
        if (component.size() > largest) {
            largest = component.size();
            result = point;
        }
    }
    if (!largest)
        throw std::runtime_error("No walkable scene arrival");
    return result;
}
std::deque<Vec> Grid::path(Vec from, Vec to) const {
    std::deque<Vec> out;
    if (!walkable(from) || !walkable(to))
        return out;
    if (segment(from, to)) {
        out.push_back(to);
        return out;
    }
    int start = int(from.y) * width + int(from.x), goal = int(to.y) * width + int(to.x);
    std::vector<float> costs(blocked.size(), std::numeric_limits<float>::infinity());
    std::vector<int> parents(blocked.size(), -1);
    Bytes closed(blocked.size());
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    auto heuristic = [&](int id) {
        int dx = std::abs(id % width - goal % width), dy = std::abs(id / width - goal / width);
        return std::max(dx, dy) + .41421356f * std::min(dx, dy);
    };
    costs[start] = 0;
    open.emplace(heuristic(start), start);
    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur])
            continue;
        if (cur == goal)
            break;
        closed[cur] = 1;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy)
                    continue;
                int x = cur % width + dx, y = cur / width + dy;
                if (!walkable(x, y))
                    continue;
                if (dx && dy && (!walkable(x - dx, y) || !walkable(x, y - dy)))
                    continue;
                int next = y * width + x;
                float cost = costs[cur] + (dx && dy ? 1.41421356f : 1.f);
                if (cost < costs[next]) {
                    costs[next] = cost;
                    parents[next] = cur;
                    open.emplace(cost + heuristic(next), next);
                }
            }
    }
    if (start != goal && parents[goal] < 0)
        return out;
    for (int cur = goal; cur != start; cur = parents[cur])
        out.push_front({cur % width + .5f, cur / width + .5f});
    out.push_front({start % width + .5f, start / width + .5f});
    out.push_back(to);
    // String-pull only across segments checked against the same collision grid.
    std::deque<Vec> smooth;
    Vec anchor = from;
    while (!out.empty()) {
        if (!segment(anchor, out.front())) return {};
        size_t far = 0;
        while (far + 1 < out.size() && segment(anchor, out[far + 1]))
            far++;
        anchor = out[far];
        smooth.push_back(anchor);
        out.erase(out.begin(), out.begin() + far + 1);
    }
    return smooth;
}
} // namespace d2x
