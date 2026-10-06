#include "navigation.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>
namespace d2x {
namespace {
template <class Clear>
bool clearSegment(Vec a, Vec b, int width, int height, Clear clear, bool blockCorners) {
    const auto inside = [=](Vec p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 &&
            p.x < width && p.y < height;
    };
    if (!inside(a) || !inside(b)) return false;
    int column = int(std::floor(a.x)), row = int(std::floor(a.y));
    const int endColumn = int(std::floor(b.x)), endRow = int(std::floor(b.y));
    if (!clear(column, row) || !clear(endColumn, endRow)) return false;
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
            if (blockCorners && (!clear(column + stepX, row) || !clear(column, row + stepY))) return false;
            column += stepX;
            row += stepY;
            crossingX += intervalX;
            crossingY += intervalY;
        }
        if (!clear(column, row)) return false;
    }
    return true;
}
} // namespace
void Grid::setObstacles(std::vector<Obstacle> next) {
    if (next == obstacles) return;
    obstacles = std::move(next);
    ++obstacleRevision;
    std::fill(objectCollision.begin(), objectCollision.end(), 0);
    std::fill(objectLightBlocked.begin(), objectLightBlocked.end(), 0);
    for (const auto &obstacle : obstacles)
        for (int y = std::max(0, obstacle.y); y < std::min(height, obstacle.y + obstacle.height); ++y)
            for (int x = std::max(0, obstacle.x); x < std::min(width, obstacle.x + obstacle.width); ++x) {
                const size_t index = size_t(y) * width + x;
                objectCollision[index] |= obstacle.mask;
                if (obstacle.blocksLight) objectLightBlocked[index] = 1;
            }
}
uint16_t Grid::objectMask(int x, int y, EntityId ignored) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0xffff;
    if (!ignored) return objectCollision[size_t(y) * width + x];
    uint16_t mask = 0;
    for (const auto &obstacle : obstacles)
        if (obstacle.id != ignored && x >= obstacle.x && y >= obstacle.y &&
            x < obstacle.x + obstacle.width && y < obstacle.y + obstacle.height)
            mask |= obstacle.mask;
    return mask;
}
uint16_t Grid::movementMask(int x, int y, EntityId ignored) const {
    for (const auto &neighbour : neighbours) {
        const int lateral = neighbour.side % 2 ? y : x;
        const int normal = neighbour.side % 2 ? x : y;
        const bool outside = neighbour.side == 1 || neighbour.side == 2
            ? normal < neighbour.plane : normal >= neighbour.plane;
        if (!outside || lateral < neighbour.start || lateral >= neighbour.end) continue;
        const int nx = x + neighbour.offsetX, ny = y + neighbour.offsetY;
        const auto &other = *neighbour.grid;
        if (nx >= 0 && ny >= 0 && nx < other.width && ny < other.height) {
            const size_t index = size_t(ny) * other.width + nx;
            return other.terrainCollision[index] | other.objectMask(nx, ny, ignored) |
                (other.blocked[index] && !(other.terrainCollision[index] & 0x09) ? 0x01 : 0);
        }
    }
    if (x < 0 || y < 0 || x >= width || y >= height) return 0xffff;
    const size_t index = size_t(y) * width + x;
    // Coarse outdoor planning grids have blocked cells without DT1 flags.
    return terrainCollision[index] | objectMask(x, y, ignored) |
        (blocked[index] && !(terrainCollision[index] & 0x09) ? 0x01 : 0);
}
bool Grid::movementClear(int x, int y, MovementCollisionRule rule, EntityId ignored) const {
    if (rule.size < 0 || rule.size > 3) return false;
    const auto clear = [&](int column, int row) { return !(movementMask(column, row, ignored) & rule.mask); };
    if (!clear(x, y)) return false;
    if (rule.size <= 1) return true;
    // D2Collision COLLISION_CheckMaskWithPattern: small paths are a cross,
    // big paths a 3x3 square. Never dilate/overwrite the underlying DT1 data.
    if (!clear(x - 1, y) || !clear(x + 1, y) || !clear(x, y - 1) || !clear(x, y + 1)) return false;
    return rule.size == 2 || (clear(x - 1, y - 1) && clear(x + 1, y - 1) &&
        clear(x - 1, y + 1) && clear(x + 1, y + 1));
}
bool Grid::segment(Vec a, Vec b, EntityId ignoredObject, MovementCollisionRule rule) const {
    return clearSegment(a, b, width, height, [&](int x, int y) {
        return movementClear(x, y, rule, ignoredObject);
    }, rule.size <= 1);
}
bool Grid::nativeMovementSegment(Vec a, Vec b, MovementCollisionRule rule) const {
    if (!walkable(a, rule) || !walkable(b, rule)) return false;
    int x = int(std::floor(a.x)), y = int(std::floor(a.y));
    const int endX = int(std::floor(b.x)), endY = int(std::floor(b.y));
    const int dx = std::abs(endX - x), dy = std::abs(endY - y);
    const int sx = endX >= x ? 1 : -1, sy = endY >= y ? 1 : -1;
    // PATH_RayTrace: diagonals test the pattern at the diagonal cell. Other
    // slopes test the major step first, with abs(minor)+1 initial deviation.
    if (dx == dy) {
        for (int i = 0; i < dx; ++i) {
            x += sx; y += sy;
            if (!movementClear(x, y, rule)) return false;
        }
        return true;
    }
    const bool horizontal = dx > dy;
    const int major = (horizontal ? dx : dy) + 1;
    const int minor = (horizontal ? dy : dx) + 1;
    int deviation = minor;
    for (int i = 0; i < major - 1; ++i) {
        if (horizontal) x += sx; else y += sy;
        if (!movementClear(x, y, rule)) return false;
        deviation += minor;
        if (deviation >= major) {
            deviation -= major;
            if (horizontal) y += sy; else x += sx;
            if (deviation && !movementClear(x, y, rule)) return false;
        }
    }
    return true;
}
bool Grid::missileSegment(Vec a, Vec b, MissileCollisionRule rule) const {
    // D2MOO COLLISION_CheckMaskWithSize: 0/1 point, 2 cross, 3 square.
    if (rule.size < 0 || rule.size > 3) return false;
    const auto clear = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        // Step.cpp stops missile travel on WALL/BARRIER only. Other mode bits
        // find units; e.g. DT1 water sharing unit bits must not stop travel.
        return !((terrainCollision[size_t(y) * width + x] | objectMask(x, y)) & rule.mask & 0x0005);
    };
    return clearSegment(a, b, width, height, [&](int x, int y) {
        if (!clear(x, y)) return false;
        if (rule.size <= 1) return true;
        if (!clear(x - 1, y) || !clear(x + 1, y) || !clear(x, y - 1) || !clear(x, y + 1)) return false;
        return rule.size == 2 || (clear(x - 1, y - 1) && clear(x + 1, y - 1) &&
            clear(x - 1, y + 1) && clear(x + 1, y + 1));
    }, false);
}
bool Grid::collisionSegment(Vec a, Vec b, uint16_t mask) const {
    return clearSegment(a, b, width, height, [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
            !((terrainCollision[size_t(y) * width + x] | objectMask(x, y)) & mask);
    }, false);
}
bool Grid::interactionSegment(Vec a, Vec b, int targetSize, EntityId target) const {
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        a.x < 0 || a.y < 0 || a.x >= width || a.y >= height ||
        b.x < 0 || b.y < 0 || b.x >= width || b.y >= height || targetSize < 0) return false;
    int x = int(std::floor(a.x)), y = int(std::floor(a.y));
    int endX = int(std::floor(b.x)), endY = int(std::floor(b.y));
    const int dx = std::abs(endX - x), dy = std::abs(endY - y);
    const int size = std::min(targetSize, 2);
    // UNITS_TestCollisionBetweenInteractingUnits removes only the interacting
    // units. UNITS_TestCollision shortens the ray on its dominant axes by
    // their sizes; the ray uses DOOR | MISSILE_BARRIER (0x0804), not walking.
    if (dx + dy < 2 + size) return true;
    if (dx >= dy) {
        const int direction = endX <= x ? -1 : 1;
        x += 2 * direction;
        endX -= size * direction;
    }
    if (dy >= dx) {
        const int direction = endY <= y ? -1 : 1;
        y += 2 * direction;
        endY -= size * direction;
    }
    // COLLISION_RayTrace advances the major axis first, with deviation=0.
    // Walking's supercover/corner checks would reject valid hand interactions.
    const int rayX = std::abs(endX - x), rayY = std::abs(endY - y);
    const int stepX = endX >= x ? 1 : -1, stepY = endY >= y ? 1 : -1;
    const int steps = std::max(rayX, rayY);
    int deviation = 0;
    for (int step = 0; step <= steps; ++step) {
        if (movementMask(x, y, target) & 0x0804) return false;
        if (step == steps) return true;
        if (rayX >= rayY) {
            x += stepX;
            deviation += rayY;
            if (deviation >= rayX) { y += stepY; deviation -= rayX; }
        } else {
            y += stepY;
            deviation += rayX;
            if (deviation >= rayY) { x += stepX; deviation -= rayY; }
        }
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
        if (x < 0 || y < 0 || x >= width || y >= height || lightBlocked[y * width + x] ||
            objectLightBlocked[size_t(y) * width + x])
            return false;
    }
    return true;
}
Bytes Grid::reachableFrom(Vec origin, MovementCollisionRule rule) const {
    Bytes reachable(blocked.size());
    if (!walkable(origin, rule)) return reachable;
    std::vector<int> pending{int(origin.y) * width + int(origin.x)};
    reachable[size_t(pending.front())] = 1;
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const int cell = pending[cursor];
        for (const auto &offset : {std::pair{0, 1}, std::pair{1, 0}, std::pair{0, -1}, std::pair{-1, 0},
                                  std::pair{-1, -1}, std::pair{-1, 1}, std::pair{1, -1}, std::pair{1, 1}}) {
            const int column = cell % width + offset.first;
            const int row = cell / width + offset.second;
            if (!walkable(column, row, rule)) continue;
            if (rule.size <= 1 && offset.first && offset.second &&
                (!walkable(column - offset.first, row, rule) ||
                 !walkable(column, row - offset.second, rule))) continue;
            const int next = row * width + column;
            if (reachable[size_t(next)]) continue;
            reachable[size_t(next)] = 1;
            pending.push_back(next);
        }
    }
    return reachable;
}
Vec Grid::nearest(Vec p, MovementCollisionRule rule) const {
    int px = std::clamp(int(p.x), 0, std::max(0, width - 1)),
        py = std::clamp(int(p.y), 0, std::max(0, height - 1));
    for (int radius = 0; radius < std::max(width, height); radius++)
        for (int y = py - radius; y <= py + radius; y++)
            for (int x = px - radius; x <= px + radius; x++)
                if ((std::abs(x - px) == radius || std::abs(y - py) == radius) && walkable(x, y, rule))
                    return {x + .5f, y + .5f};
    return p;
}
Vec Grid::inspectionArrival() const {
    Bytes visited(blocked.size());
    std::vector<int> component;
    size_t largest = 0;
    Vec result{};
    for (int start = 0; start < int(blocked.size()); ++start) {
        if (!walkable(start % width, start / width, playerMovement) || visited[start])
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
                    interior &= walkable(x + dx, y + dy, playerMovement);
            float dx = x + .5f - width * .5f, dy = y + .5f - height * .5f;
            float score = dx * dx + dy * dy + (interior ? 0.f : float(width * width + height * height));
            if (score < best) {
                best = score;
                point = {x + .5f, y + .5f};
            }
            for (const auto &[ox, oy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                       std::pair{-1, -1}, std::pair{-1, 1}, std::pair{1, -1}, std::pair{1, 1}}) {
                if (!walkable(x + ox, y + oy, playerMovement))
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
std::deque<Vec> Grid::path(Vec from, Vec to, bool allowPartial, MovementCollisionRule rule, bool nativeSegments) const {
    std::deque<Vec> out;
    if (!walkable(from, rule) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
        to.x < 0 || to.y < 0 || to.x >= width || to.y >= height || (!allowPartial && !walkable(to, rule)))
        return out;
    const auto clearLine = [&](Vec a, Vec b) {
        return segment(a, b, {}, rule) && (!nativeSegments || nativeMovementSegment(a, b, rule));
    };
    if (clearLine(from, to)) {
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
    float nearestPossible = 0;
    if (allowPartial && !walkable(to, rule)) {
        nearestPossible = std::numeric_limits<float>::infinity();
        const int gx = goal % width, gy = goal / width;
        for (int radius = 1; radius <= std::max(width, height) && radius <= nearestPossible; ++radius)
            for (int y = std::max(0, gy - radius); y <= std::min(height - 1, gy + radius); ++y)
                for (int x = std::max(0, gx - radius); x <= std::min(width - 1, gx + radius); ++x)
                    if ((std::abs(x - gx) == radius || std::abs(y - gy) == radius) && walkable(x, y, rule))
                        nearestPossible = std::min(nearestPossible, heuristic(y * width + x));
    }
    costs[start] = 0;
    int closest = start;
    open.emplace(heuristic(start), start);
    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur])
            continue;
        // Native AStar retains its best reachable node when the goal is blocked.
        if (heuristic(cur) < heuristic(closest) ||
            (heuristic(cur) == heuristic(closest) && costs[cur] < costs[closest]))
            closest = cur;
        if (cur == goal || (allowPartial && heuristic(cur) <= nearestPossible))
            break;
        closed[cur] = 1;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy)
                    continue;
                int x = cur % width + dx, y = cur / width + dy;
                if (!walkable(x, y, rule))
                    continue;
                if (rule.size <= 1 && dx && dy && (!walkable(x - dx, y, rule) || !walkable(x, y - dy, rule)))
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
    if (start != goal && parents[goal] < 0) {
        if (!allowPartial || closest == start) return out;
        goal = closest;
        to = {goal % width + .5f, goal / width + .5f};
    }
    for (int cur = goal; cur != start; cur = parents[cur])
        out.push_front({cur % width + .5f, cur / width + .5f});
    out.push_front({start % width + .5f, start / width + .5f});
    out.push_back(to);
    // String-pull only across segments checked against the same collision grid.
    std::deque<Vec> smooth;
    Vec anchor = from;
    while (!out.empty()) {
        if (!clearLine(anchor, out.front())) return {};
        size_t far = 0;
        while (far + 1 < out.size() && clearLine(anchor, out[far + 1]))
            far++;
        anchor = out[far];
        smooth.push_back(anchor);
        out.erase(out.begin(), out.begin() + far + 1);
    }
    return smooth;
}
} // namespace d2x
