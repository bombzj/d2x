#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include "core/id.hpp"
#include <deque>
namespace d2x {
struct MissileCollisionRule {
    uint16_t mask = 0;
    int size = 0;
};
struct RoomBounds {
    int x, y, width, height;
    bool populate;
};
// Immutable spatial room index. Activity is derived from the observer's room and
// touching neighbours, following DRLG's InRoom / InSight / OutOfSight model.
class RoomLayout {
    int width_ = 0, height_ = 0;
    std::vector<RoomBounds> rooms_;
    std::vector<int> cells_;
    int roomAt(Vec point) const;

  public:
    RoomLayout() = default;
    RoomLayout(int width, int height, std::vector<RoomBounds> rooms);
    const RoomBounds *room(Vec point) const;
    bool nearby(Vec observer, Vec point) const;
};
struct Grid {
    int width = 0, height = 0;
    Bytes blocked;
    Bytes lightBlocked;
    Bytes terrainCollision;
    struct Obstacle {
        EntityId id;
        int x, y, width, height;
        uint16_t mask;
        bool operator==(const Obstacle &) const = default;
    };
    std::vector<Obstacle> obstacles;
    std::vector<uint16_t> objectCollision;
    Grid() = default;
    Grid(int w, int h) : width(w), height(h), blocked(size_t(w) * h), lightBlocked(size_t(w) * h),
                         terrainCollision(size_t(w) * h), objectCollision(size_t(w) * h) {}
    void setObstacles(std::vector<Obstacle> next);
    uint16_t objectMask(int x, int y, EntityId ignored = {}) const;
    bool walkable(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && !blocked[y * width + x] &&
            !(objectCollision[size_t(y) * width + x] & 0x0c00);
    }
    bool walkable(Vec p) const {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 &&
            p.x < width && p.y < height && walkable(int(std::floor(p.x)), int(std::floor(p.y)));
    }
    bool segment(Vec a, Vec b, EntityId ignoredObject = {}) const;
    bool missileSegment(Vec a, Vec b, MissileCollisionRule rule) const;
    bool lightSegment(Vec a, Vec b) const;
    Bytes reachableFrom(Vec origin) const;
    Vec nearest(Vec p) const;
    // Scene inspection arrival: an interior point of the largest walkable component.
    // Real level transitions must use the original linked warp coordinates instead.
    Vec inspectionArrival() const;
    // Partial routes are for movement requests; reachability checks stay exact.
    std::deque<Vec> path(Vec from, Vec to, bool allowPartial = false) const;
};
} // namespace d2x
