#include "navigation.hpp"
#include "native_spawn.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <stdexcept>
namespace d2x {
namespace {
int nativeDirectionIndex(int dx, int dy) {
    const int ax = std::abs(dx), ay = std::abs(dy);
    if (ax < 2 * ay) {
        if (ay >= 2 * ax) {
            if (dx < 0) return dy < -1 ? 5 : std::min(dy, 2) + 7;
            dx &= 1;
        }
    } else dy = dy >= 0 ? dy & 1 : -1;
    dx = std::clamp(dx, -2, 2);
    return dy < -1 ? 5 * dx + 10 : std::min(dy, 2) + 5 * dx + 12;
}
std::optional<Vec> preparedPlayerTarget(const Grid &grid, Vec from, Vec to, MovementCollisionRule rule) {
    const int sx = int(std::floor(from.x)), sy = int(std::floor(from.y));
    int x = int(std::floor(to.x)), y = int(std::floor(to.y));
    if (grid.movementClear(x, y, rule)) return to;
    // Straight's 0x1000 policy in D2MOO Path::D2Common_10142 and
    // PathMisc::PATH_PreparePathTargetForPathUpdate (MIT; docs/licenses/D2MOO.txt).
    // Three cardinal rays start at the obstructed target, towards the player.
    constexpr std::array<std::array<int, 3>, 25> priorities{{
        {4,6,-1},{4,6,-1},{4,2,6},{4,2,-1},{4,2,-1},
        {6,4,-1},{4,6,-1},{4,2,6},{4,2,-1},{2,4,-1},
        {6,0,4},{6,0,4},{6,0,4},{2,0,4},{2,0,4},
        {6,0,-1},{0,6,-1},{0,2,6},{2,0,-1},{2,0,-1},
        {0,6,-1},{0,6,-1},{0,2,6},{0,2,-1},{0,2,-1}}};
    constexpr std::array<int, 8> offsetX{1,1,0,-1,-1,-1,0,1}, offsetY{0,1,1,1,0,-1,-1,-1};
    auto directions = priorities[size_t(nativeDirectionIndex(sx-x, sy-y))];
    std::array<std::array<int, 2>, 3> points{{{x,y},{x,y},{x,y}}};
    while (points[0][0] != sx || points[0][1] != sy) {
        for (size_t index = 0; index < points.size(); ++index) {
            const int direction = directions[index];
            if (direction < 0) continue;
            auto &point = points[index];
            point[0] += offsetX[size_t(direction)]; point[1] += offsetY[size_t(direction)];
            if (!grid.movementClear(point[0], point[1], rule)) continue;
            // The native routine checks its primary ray even if a secondary
            // ray found a free cell on the same iteration.
            if (points[0][0] == sx && points[0][1] == sy) return {};
            x = point[0]; y = point[1];
            const int dx = x-sx, dy = y-sy, ax = std::abs(dx), ay = std::abs(dy);
            if (ax < 5 && ay < 5 && (dx || dy)) {
                // Independent derivation of Path's 9x9 cardinal-snap table.
                const int stepX = ax >= std::max(1, ay) ? (dx < 0 ? -1 : 1) : 0;
                const int stepY = ay >= std::max(1, ax) ? (dy < 0 ? -1 : 1) : 0;
                int distance = std::max(ax, ay);
                while (distance < 5 && grid.movementClear(x+stepX, y+stepY, rule)) {
                    x += stepX; y += stepY; ++distance;
                }
            }
            return Vec{float(x)+.5f, float(y)+.5f};
        }
        directions[0] = priorities[size_t(nativeDirectionIndex(sx-points[0][0], sy-points[0][1]))][0];
    }
    return {};
}
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
void Grid::releaseStorage() {
    decltype(blocked){}.swap(blocked);decltype(lightBlocked){}.swap(lightBlocked);
    decltype(terrainCollision){}.swap(terrainCollision);decltype(fullTerrainCollision){}.swap(fullTerrainCollision);
    decltype(objectCollision){}.swap(objectCollision);decltype(objectLightBlocked){}.swap(objectLightBlocked);
    decltype(missileCollision){}.swap(missileCollision);decltype(nativeCollision){}.swap(nativeCollision);
    ++obstacleRevision;++missileCollisionRevision;++nativeCollisionRevision;
}
void Grid::restoreStorage() {
    if(!terrainCollision.empty()) return;
    const auto count=size_t(width)*height;
    blocked.assign(count,1);lightBlocked.assign(count,1);terrainCollision.assign(count,0xff);
    fullTerrainCollision.assign(count,0xffff);objectCollision.assign(count,0);objectLightBlocked.assign(count,0);
    missileCollision.assign(count,0);nativeCollision.assign(count,0xffff);
    auto saved=std::move(obstacles);obstacles.clear();setObstacles(std::move(saved));
    ++obstacleRevision;++missileCollisionRevision;++nativeCollisionRevision;
}
void Grid::setObstacles(std::vector<Obstacle> next) {
    // Client A8880 tests the native collision mask 0x22. Objects.BlocksLight
    // controls a unit drawing flag; it never adds a collision footprint.
    for (auto &obstacle : next) obstacle.blocksLight = (obstacle.mask & 0x22) != 0;
    if (next == obstacles) return;
    if(!nativeCollision.empty()) {
        const auto changed=[](const auto &list,const Obstacle &value) {
            return std::find(list.begin(),list.end(),value)==list.end();
        };
        const auto write=[&](const Obstacle &obstacle,bool occupied) {
            for(int y=std::max(0,obstacle.y);y<std::min(height,obstacle.y+obstacle.height);++y)
                for(int x=std::max(0,obstacle.x);x<std::min(width,obstacle.x+obstacle.width);++x) {
                    auto &cell=nativeCollision[size_t(y)*width+x];const auto previous=cell;
                    cell=occupied?uint16_t(cell|obstacle.mask):uint16_t(cell&~obstacle.mask);
                    if(previous!=cell) ++nativeCollisionRevision;
                }
        };
        for(const auto &old:obstacles) if(changed(next,old)) write(old,false);
        for(const auto &value:next) if(changed(obstacles,value)) write(value,true);
    }
    obstacles = std::move(next);
    ++obstacleRevision;
    if(objectCollision.empty()) return;
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
    if (x < 0 || y < 0 || x >= width || y >= height || objectCollision.empty()) return 0xffff;
    if (!ignored) return objectCollision[size_t(y) * width + x];
    uint16_t mask = 0;
    for (const auto &obstacle : obstacles)
        if (obstacle.id != ignored && x >= obstacle.x && y >= obstacle.y &&
            x < obstacle.x + obstacle.width && y < obstacle.y + obstacle.height)
            mask |= obstacle.mask;
    return mask;
}
uint16_t Grid::movementMask(int x, int y, EntityId ignored) const {
    if(collisionSample) return collisionSample(x,y,ignored);
    if (!navigationSources.empty()) {
        for (const auto &source : navigationSources) {
            const auto &grid = *source.grid;
            if(grid.terrainCollision.empty()) continue;
            const int column = x + source.offsetX, row = y + source.offsetY;
            if (column < 0 || row < 0 || column >= grid.width || row >= grid.height) continue;
            const size_t index = size_t(row) * grid.width + column;
            const auto terrain = grid.fullTerrainCollision.empty() ? grid.terrainCollision[index] : grid.fullTerrainCollision[index];
            if (terrain == 0xffff) continue; // No materialized collision room here.
            return terrain | grid.objectMask(column, row, ignored) | (grid.missileCollision.empty()?0:grid.missileCollision[index]) |
                (grid.blocked[index] && !(grid.terrainCollision[index] & 0x09) ? 0x01 : 0);
        }
        return 0xffff;
    }
    // D2Collision resolves the owning room before consulting adjacent rooms.
    if (x >= 0 && y >= 0 && x < width && y < height && !terrainCollision.empty()) {
        const size_t index = size_t(y) * width + x;
        const auto terrain = fullTerrainCollision.empty() ? terrainCollision[index] : fullTerrainCollision[index];
        if (terrain != 0xffff) return terrain | objectMask(x, y, ignored) | (missileCollision.empty()?0:missileCollision[index]) |
            (blocked[index] && !(terrainCollision[index] & 0x09) ? 0x01 : 0);
    }
    for (const auto &neighbour : neighbours) {
        const int lateral = neighbour.side % 2 ? y : x;
        const int normal = neighbour.side % 2 ? x : y;
        const bool outside = neighbour.side == 1 || neighbour.side == 2
            ? normal < neighbour.plane : normal >= neighbour.plane;
        if (!outside || lateral < neighbour.start || lateral >= neighbour.end) continue;
        const int nx = x + neighbour.offsetX, ny = y + neighbour.offsetY;
        const auto &other = *neighbour.grid;
        if(other.terrainCollision.empty()) continue;
        if (nx >= 0 && ny >= 0 && nx < other.width && ny < other.height) {
            const size_t index = size_t(ny) * other.width + nx;
            const uint16_t terrain = other.fullTerrainCollision.empty()
                ? other.terrainCollision[index] : other.fullTerrainCollision[index];
            return terrain | other.objectMask(nx, ny, ignored) | (other.missileCollision.empty()?0:other.missileCollision[index]) |
                (other.blocked[index] && !(other.terrainCollision[index] & 0x09) ? 0x01 : 0);
        }
    }
    if (x < 0 || y < 0 || x >= width || y >= height || terrainCollision.empty()) return 0xffff;
    const size_t index = size_t(y) * width + x;
    // Coarse outdoor planning grids have blocked cells without DT1 flags.
    const uint16_t terrain = fullTerrainCollision.empty() ? terrainCollision[index] : fullTerrainCollision[index];
    return terrain | objectMask(x, y, ignored) | (missileCollision.empty()?0:missileCollision[index]) |
        (blocked[index] && !(terrainCollision[index] & 0x09) ? 0x01 : 0);
}
Grid Grid::linkedNavigation(Vec &origin) const {
    // Path.cpp prepares the target room; D2Collision queries room collision
    // across active near-room links. Keep those grids borrowed, not copied.
    std::vector<NavigationSource> sources{{this, 0, 0}};
    int left = 0, top = 0, right = width, bottom = height;
    for (size_t index = 0; index < sources.size(); ++index) {
        const auto source = sources[index];
        for (const auto &edge : source.grid->neighbours) {
            const int x = source.offsetX + edge.offsetX, y = source.offsetY + edge.offsetY;
            const auto found = std::find_if(sources.begin(), sources.end(), [&](const auto &entry) { return entry.grid == edge.grid; });
            if (found != sources.end()) {
                if (found->offsetX != x || found->offsetY != y) throw std::logic_error("Inconsistent linked collision origins");
                continue;
            }
            sources.push_back({edge.grid, x, y});
            left = std::min(left, -x); top = std::min(top, -y);
            right = std::max(right, edge.grid->width - x); bottom = std::max(bottom, edge.grid->height - y);
        }
    }
    Grid view; view.width = right - left; view.height = bottom - top;
    origin = {float(left), float(top)};
    for (auto &source : sources) { source.offsetX += left; source.offsetY += top; }
    view.navigationSources = std::move(sources);
    view.collisionQuery=collisionQuery;
    if(collisionSample) view.collisionSample=[sample=collisionSample,left,top](int x,int y,EntityId ignored) {
        return sample(x+left,y+top,ignored);
    };
    return view;
}
uint16_t Grid::nativeMovementMask(int x,int y,EntityId ignored) const {
    const auto sample=[&](const Grid &grid,int column,int row) {
        if(grid.nativeCollision.empty()) return grid.movementMask(column,row,ignored);
        uint16_t value=grid.nativeCollision[size_t(row)*grid.width+column];
        if(ignored) for(const auto &obstacle:grid.obstacles)
            if(obstacle.id==ignored && column>=obstacle.x && row>=obstacle.y &&
                column<obstacle.x+obstacle.width && row<obstacle.y+obstacle.height) value&=uint16_t(~obstacle.mask);
        return value;
    };
    if(!navigationSources.empty()) {
        for(const auto &source:navigationSources) {
            const auto &grid=*source.grid;const int column=x+source.offsetX,row=y+source.offsetY;
            if(grid.terrainCollision.empty()) continue;
            if(column<0 || row<0 || column>=grid.width || row>=grid.height) continue;
            if(!grid.fullTerrainCollision.empty() && grid.fullTerrainCollision[size_t(row)*grid.width+column]==0xffff) continue;
            return sample(grid,column,row);
        }
        return 0xffff;
    }
    if(x>=0 && y>=0 && x<width && y<height &&
        (fullTerrainCollision.empty() || fullTerrainCollision[size_t(y)*width+x]!=0xffff)) return sample(*this,x,y);
    for(const auto &edge:neighbours) {
        const int lateral=edge.side%2?y:x,normal=edge.side%2?x:y;
        const bool outside=edge.side==1 || edge.side==2?normal<edge.plane:normal>=edge.plane;
        if(!outside || lateral<edge.start || lateral>=edge.end) continue;
        const int column=x+edge.offsetX,row=y+edge.offsetY;const auto &other=*edge.grid;
        if(column>=0 && row>=0 && column<other.width && row<other.height) return sample(other,column,row);
    }
    return x>=0 && y>=0 && x<width && y<height?sample(*this,x,y):uint16_t(0xffff);
}
bool Grid::navigationContains(Vec point) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    Vec origin;
    const auto view = linkedNavigation(origin);
    point = point - origin;
    return point.x >= 0 && point.y >= 0 && point.x < view.width && point.y < view.height &&
        view.movementMask(int(std::floor(point.x)), int(std::floor(point.y))) != 0xffff;
}
uint64_t Grid::navigationRevision() const {
    if (neighbours.empty()) return obstacleRevision*0x100000001b3ULL+missileCollisionRevision;
    Vec origin; const auto view = linkedNavigation(origin);
    uint64_t revision = view.navigationSources.size();
    for (const auto &source : view.navigationSources) {
        revision = revision * 0x100000001b3ULL + source.grid->obstacleRevision;
        revision = revision * 0x100000001b3ULL + source.grid->missileCollisionRevision;
    }
    return revision;
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
    const CollisionQueryScope collisionScope{*this};
    if (!neighbours.empty()) {
        Vec origin; const auto view = linkedNavigation(origin);
        return view.segment(a - origin, b - origin, ignoredObject, rule);
    }
    return clearSegment(a, b, width, height, [&](int x, int y) {
        return movementClear(x, y, rule, ignoredObject);
    }, rule.size <= 1);
}
bool Grid::nativeMovementSegment(Vec a, Vec b, MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    if (!walkable(a, rule) || !walkable(b, rule)) return false;
    const auto target = nativeMovementTarget(a, b, rule);
    return int(std::floor(target.x)) == int(std::floor(b.x)) &&
        int(std::floor(target.y)) == int(std::floor(b.y));
}
Vec Grid::nativeMovementTarget(Vec a, Vec b, MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    if (!walkable(a, rule) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        b.x < 0 || b.y < 0 || b.x >= width || b.y >= height) return a;
    int x = int(std::floor(a.x)), y = int(std::floor(a.y));
    Vec last{float(x) + .5f, float(y) + .5f};
    const int endX = int(std::floor(b.x)), endY = int(std::floor(b.y));
    const int dx = std::abs(endX - x), dy = std::abs(endY - y);
    const int sx = endX >= x ? 1 : -1, sy = endY >= y ? 1 : -1;
    // PATH_RayTrace: diagonals test the pattern at the diagonal cell. Other
    // slopes test the major step first, with abs(minor)+1 initial deviation.
    if (dx == dy) {
        for (int i = 0; i < dx; ++i) {
            x += sx; y += sy;
            if (!movementClear(x, y, rule)) return last;
            last = {float(x) + .5f, float(y) + .5f};
        }
        return last;
    }
    const bool horizontal = dx > dy;
    const int major = (horizontal ? dx : dy) + 1;
    const int minor = (horizontal ? dy : dx) + 1;
    int deviation = minor;
    for (int i = 0; i < major - 1; ++i) {
        if (horizontal) x += sx; else y += sy;
        if (!movementClear(x, y, rule)) return last;
        deviation += minor;
        if (deviation >= major) {
            deviation -= major;
            if (horizontal) y += sy; else x += sx;
            if (deviation && !movementClear(x, y, rule)) return last;
        }
        last = {float(x) + .5f, float(y) + .5f};
    }
    // PATH_RayTrace only writes its destination on collision. Its biased
    // raster can visit a neighbouring cell on the last major-axis step;
    // that cell is not the path target when the whole ray succeeds.
    return {float(endX) + .5f, float(endY) + .5f};
}
std::deque<Vec> Grid::nativePlayerPath(Vec from, Vec to, MovementCollisionRule rule, uint8_t targetType) const {
    const CollisionQueryScope collisionScope{*this};
    if (!neighbours.empty()) {
        Vec origin; const auto view = linkedNavigation(origin);
        auto route = view.nativePlayerPath(from - origin, to - origin, rule, targetType);
        for (auto &point : route) point = point + origin;
        return route;
    }
    if (!walkable(from, rule) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
        to.x < 0 || to.y < 0 || to.x >= width || to.y >= height) return {};
    const auto target = preparedPlayerTarget(*this, from, to, rule);
    return target ? nativeUnitPath(from,*target,7,73,rule,targetType!=255,targetType==2 || targetType==4?2:1)
                  : std::deque<Vec>{};
}
std::deque<Vec> Grid::nativeUnitPath(Vec from, Vec to, uint8_t type, uint8_t maxDistance, MovementCollisionRule rule, bool unitTarget, uint8_t targetTolerance) const {
    const CollisionQueryScope collisionScope{*this};
    if (!neighbours.empty()) {
        Vec origin; const auto view = linkedNavigation(origin);
        auto route = view.nativeUnitPath(from - origin, to - origin, type, maxDistance, rule, unitTarget, targetTolerance);
        for (auto &point : route) point = point + origin;
        return route;
    }
    if (!walkable(from, rule) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
        to.x < 0 || to.y < 0 || to.x >= width || to.y >= height) return {};
    if(type==1) return nativeAStarPath(from,to,rule,unitTarget);
    if(type==15) return nativeWallFollowPath(from,to,unitTarget?std::max(uint8_t(40),maxDistance):maxDistance,rule);
    if(type!=2 && type!=7 && type!=13) return {};
    // D2MOO PathMisc (MIT, docs/licenses/D2MOO.txt): Straight first uses
    // the biased collision ray, then Toward's three direction priorities.
    // The 73-step player bound and 18-cell AStar scope are path policy,
    // not artificial packet destinations or authoritative movement.
    constexpr std::array<std::array<int,3>,25> directions{{
        {5,4,6},{4,5,6},{4,3,5},{4,3,2},{3,4,2},
        {6,5,4},{5,4,6},{4,3,5},{3,4,2},{2,3,4},
        {6,7,5},{6,7,5},{6,7,5},{2,1,3},{2,1,3},
        {6,7,0},{7,0,6},{0,1,7},{1,0,2},{2,1,0},
        {7,0,6},{0,7,6},{0,1,7},{0,1,2},{1,0,2}}};
    constexpr std::array<int,8> offsetX{1,1,0,-1,-1,-1,0,1}, offsetY{0,1,1,1,0,-1,-1,-1};
    constexpr std::array<int,64> distances{
        -1,-1,-1,0,2,4,6,8, -1,-1,0,1,2,4,6,8,
        -1,0,0,2,3,5,7,8, 0,1,2,2,4,5,7,8,
        2,2,3,4,5,6,7,9, 4,4,5,5,6,7,8,9,
        6,6,7,7,7,8,10,10, 8,8,8,8,9,9,10,11};
    const int gx = int(std::floor(to.x)), gy = int(std::floor(to.y));
    const int sx = int(std::floor(from.x)), sy = int(std::floor(from.y));
    auto distance = [&](int x, int y) {
        const int dx = std::abs(gx-x), dy = std::abs(gy-y);
        return dx >= 8 || dy >= 8 ? std::max(dx,dy)*2 + std::min(dx,dy)
            : std::max(0, distances[size_t(dx+8*dy)] + 1);
    };
    const auto clipped = nativeMovementTarget(from, to, rule);
    int x = int(std::floor(clipped.x)), y = int(std::floor(clipped.y));
    std::deque<Vec> toward;
    if (x != sx || y != sy) toward.push_back(clipped);
    if (distance(x,y) <= targetTolerance) {
        if(!toward.empty()) return toward;
        const int dx=gx-sx,dy=gy-sy;
        return type==7 && dx*dx+dy*dy<=18*18 ? nativeAStarPath(from,to,rule,unitTarget) : toward;
    }
    int previous = -1;
    bool lastTurn = false;
    int steps = 0;
    for (; steps < maxDistance && (x != gx || y != gy); ++steps) {
        lastTurn = false;
        const int index = nativeDirectionIndex(gx-x,gy-y);
        int selected = -1;
        for (int d : directions[size_t(index)])
            if (movementClear(x+offsetX[d],y+offsetY[d],rule)) { selected=d; break; }
        if (selected < 0 || ((selected+4)&7) == previous) break;
        lastTurn = selected != previous;
        if (lastTurn && (x != sx || y != sy)) toward.push_back({float(x)+.5f,float(y)+.5f});
        x += offsetX[selected]; y += offsetY[selected]; previous=selected;
    }
    if (steps && !lastTurn) toward.push_back({float(x)+.5f,float(y)+.5f});
    if (!toward.empty() && distance(int(toward.back().x),int(toward.back().y)) <= targetTolerance) return toward;
    const int dx = gx-sx, dy = gy-sy;
    if (type==7 && dx*dx + dy*dy <= 18*18) {
        auto detour = nativeAStarPath(from,to,rule,unitTarget);
        if (!detour.empty()) return detour;
    }
    return toward;
}
std::deque<Vec> Grid::nativeMonsterCirclePath(Vec from,Vec target,int distance,int turn,MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    // D2MOO PathMisc sub_6FDAA720 / Path type 5 and 6 (MIT,
    // docs/licenses/D2MOO.txt): rotate native direction priorities by +/-2,
    // test each cell and stop on an obstructed or reversed direction.
    std::deque<Vec> route;
    if(!walkable(from,rule) || !std::isfinite(target.x) || !std::isfinite(target.y) ||
       distance<=0 || distance>77 || (turn!=-1 && turn!=1)) return route;
    constexpr std::array<std::array<int,3>,25> directions{{
        {5,4,6},{4,5,6},{4,3,5},{4,3,2},{3,4,2},
        {6,5,4},{5,4,6},{4,3,5},{3,4,2},{2,3,4},
        {6,7,5},{6,7,5},{6,7,5},{2,1,3},{2,1,3},
        {6,7,0},{7,0,6},{0,1,7},{1,0,2},{1,0,2},
        {7,0,6},{0,7,6},{0,1,7},{0,1,2},{1,0,2}}};
    constexpr std::array<int,8> ox{1,1,0,-1,-1,-1,0,1},oy{0,1,1,1,0,-1,-1,-1};
    int x=int(std::floor(from.x)),y=int(std::floor(from.y)),previous=-1;
    const int tx=int(std::floor(target.x)),ty=int(std::floor(target.y));
    for(int step=0;step<distance && (x!=tx || y!=ty);++step) {
        int dx=tx-x,dy=ty-y;
        const int ax=std::abs(dx),ay=std::abs(dy);
        if(ax<2*ay) {if(ay>=2*ax) dx=dx<0?-1:dx&1;}
        else dy=dy>=0?dy&1:-1;
        dx=std::clamp(dx,-2,2);
        const int index=dy< -1?5*dx+10:std::min(dy,2)+5*dx+12;
        int selected=-1;
        for(int direction:directions[size_t(index)]) {
            const int d=(direction+2*turn+8)%8;
            if(walkable(x+ox[d],y+oy[d],rule)) {selected=d;break;}
        }
        if(selected<0 || ((selected+4)%8)==previous) break;
        x+=ox[selected];y+=oy[selected];previous=selected;
        route.push_back({float(x)+.5f,float(y)+.5f});
    }
    return route;
}
bool Grid::missileSegment(Vec a, Vec b, MissileCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    // D2MOO COLLISION_CheckMaskWithSize: 0/1 point, 2 cross, 3 square.
    if (rule.size < 0 || rule.size > 3) return false;
    const auto clear = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height || terrainCollision.empty()) return false;
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
    const CollisionQueryScope collisionScope{*this};
    if (!neighbours.empty()) {
        Vec origin; const auto view = linkedNavigation(origin);
        return view.collisionSegment(a - origin, b - origin, mask);
    }
    return clearSegment(a, b, width, height, [&](int x, int y) {
        return !(movementMask(x, y) & mask);
    }, false);
}
bool Grid::interactionSegment(Vec a, Vec b, int targetSize, EntityId target,uint16_t mask,int sourceSize) const {
    const CollisionQueryScope collisionScope{*this};
    if (!neighbours.empty()) {
        Vec origin; const auto view = linkedNavigation(origin);
        return view.interactionSegment(a - origin, b - origin, targetSize, target,mask,sourceSize);
    }
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        a.x < 0 || a.y < 0 || a.x >= width || a.y >= height ||
        b.x < 0 || b.y < 0 || b.x >= width || b.y >= height || targetSize < 0 || sourceSize<0) return false;
    int x = int(std::floor(a.x+.5f)), y = int(std::floor(a.y+.5f));
    int endX = int(std::floor(b.x+.5f)), endY = int(std::floor(b.y+.5f));
    const int dx = std::abs(endX - x), dy = std::abs(endY - y);
    const int size = std::min(targetSize, 2);
    sourceSize=std::min(sourceSize,2);
    // UNITS_TestCollisionBetweenInteractingUnits removes only the interacting
    // units. UNITS_TestCollision shortens the ray on its dominant axes by
    // their sizes; the ray uses DOOR | MISSILE_BARRIER (0x0804), not walking.
    if (dx + dy < sourceSize + size) return true;
    if (dx >= dy) {
        const int direction = endX <= x ? -1 : 1;
        x += sourceSize * direction;
        endX -= size * direction;
    }
    if (dy >= dx) {
        const int direction = endY <= y ? -1 : 1;
        y += sourceSize * direction;
        endY -= size * direction;
    }
    // COLLISION_RayTrace advances the major axis first, with deviation=0.
    // Walking's supercover/corner checks would reject valid hand interactions.
    const int rayX = std::abs(endX - x), rayY = std::abs(endY - y);
    const int stepX = endX >= x ? 1 : -1, stepY = endY >= y ? 1 : -1;
    const int steps = std::max(rayX, rayY);
    int deviation = 0;
    for (int step = 0; step <= steps; ++step) {
        if (movementMask(x, y, target) & mask) return false;
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
    const CollisionQueryScope collisionScope{*this};
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
        if (x < 0 || y < 0 || x >= width || y >= height || lightBlocked.empty() || lightBlocked[y * width + x] ||
            objectLightBlocked[size_t(y) * width + x])
            return false;
    }
    return true;
}
Bytes Grid::reachableFrom(Vec origin, MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    Bytes reachable(size_t(width)*height);
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
    const CollisionQueryScope collisionScope{*this};
    int px = std::clamp(int(p.x), 0, std::max(0, width - 1)),
        py = std::clamp(int(p.y), 0, std::max(0, height - 1));
    for (int radius = 0; radius < std::max(width, height); radius++)
        for (int y = py - radius; y <= py + radius; y++)
            for (int x = px - radius; x <= px + radius; x++)
                if ((std::abs(x - px) == radius || std::abs(y - py) == radius) && walkable(x, y, rule))
                    return {x + .5f, y + .5f};
    return p;
}
std::optional<Vec> Grid::nativeSpawn(Vec p, int maxDistance, MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 ||
        p.x >= width || p.y >= height || maxDistance <= 0) return {};
    const auto result=nativeSpawnCoordinate(int(p.x),int(p.y),maxDistance,[&](int x,int y){return walkable(x,y,rule);});
    return result?std::optional<Vec>{Vec{float(result->first),float(result->second)}}:std::nullopt;
}
Vec Grid::inspectionArrival() const {
    const CollisionQueryScope collisionScope{*this};
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
    const CollisionQueryScope collisionScope{*this};
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
    const auto cellCount=size_t(width)*height;
    std::vector<float> costs(cellCount, std::numeric_limits<float>::infinity());
    std::vector<int> parents(cellCount, -1);
    Bytes closed(cellCount);
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
