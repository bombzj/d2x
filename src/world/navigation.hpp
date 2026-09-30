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
struct MovementCollisionRule {
    uint16_t mask = 0x1c09; // WALL | NOPLAYER | OBJECT | DOOR | NO_PATH.
    int size = 1; // Point for spatial queries; dynamic path patterns use cross/square.
};
inline constexpr MovementCollisionRule playerMovement{0x1c09, 2};
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
    std::vector<const RoomBounds *> nearRooms(const RoomBounds &observer) const;
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
        bool blocksLight = false;
        bool operator==(const Obstacle &) const = default;
    };
    std::vector<Obstacle> obstacles;
    std::vector<uint16_t> objectCollision;
    Bytes objectLightBlocked;
    uint64_t obstacleRevision = 0;
    // Borrow only direct walking neighbours after region storage is complete.
    // DS1's extra edge row is drawable terrain, not a duplicate collision room.
    struct BoundaryNeighbour {
        const Grid *grid;
        int offsetX, offsetY, side, plane, start, end;
    };
    std::vector<BoundaryNeighbour> neighbours;
    Grid() = default;
    Grid(int w, int h) : width(w), height(h), blocked(size_t(w) * h), lightBlocked(size_t(w) * h),
                         terrainCollision(size_t(w) * h), objectCollision(size_t(w) * h),
                         objectLightBlocked(size_t(w) * h) {}
    void setObstacles(std::vector<Obstacle> next);
    uint16_t objectMask(int x, int y, EntityId ignored = {}) const;
    uint16_t movementMask(int x, int y, EntityId ignored = {}) const;
    bool movementClear(int x, int y, MovementCollisionRule rule, EntityId ignored = {}) const;
    bool walkable(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && !blocked[y * width + x] &&
            !(objectCollision[size_t(y) * width + x] & 0x1c00);
    }
    bool walkable(Vec p) const {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 &&
            p.x < width && p.y < height && walkable(int(std::floor(p.x)), int(std::floor(p.y)));
    }
    bool walkable(int x, int y, MovementCollisionRule rule) const {
        return x >= 0 && y >= 0 && x < width && y < height && movementClear(x, y, rule);
    }
    bool walkable(Vec p, MovementCollisionRule rule) const {
        return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 &&
            p.x < width && p.y < height && walkable(int(std::floor(p.x)), int(std::floor(p.y)), rule);
    }
    bool segment(Vec a, Vec b, EntityId ignoredObject = {}, MovementCollisionRule rule = {}) const;
    bool collisionSegment(Vec a, Vec b, uint16_t mask) const;
    // Object interaction uses the native shortened integer ray and flying
    // player mask; approaching it still uses the full walking footprint.
    bool interactionSegment(Vec a, Vec b, int targetSize, EntityId target) const;
    bool missileSegment(Vec a, Vec b, MissileCollisionRule rule) const;
    bool lightSegment(Vec a, Vec b) const;
    Bytes reachableFrom(Vec origin, MovementCollisionRule rule = {}) const;
    Vec nearest(Vec p, MovementCollisionRule rule = {}) const;
    // Scene inspection arrival: an interior point of the largest walkable component.
    // Real level transitions must use the original linked warp coordinates instead.
    Vec inspectionArrival() const;
    // Partial routes are for movement requests; reachability checks stay exact.
    std::deque<Vec> path(Vec from, Vec to, bool allowPartial = false, MovementCollisionRule rule = {}) const;
};
} // namespace d2x
