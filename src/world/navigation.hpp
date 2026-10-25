#pragma once
#include "core/bytes.hpp"
#include "world/collision.hpp"
#include "world/room_layout.hpp"
#include "core/math.hpp"
#include "core/id.hpp"
#include <deque>
#include <functional>
#include <optional>
#include <vector>
namespace d2x {
struct Grid {
    int width = 0, height = 0;
    Bytes blocked;
    Bytes lightBlocked;
    Bytes terrainCollision;
    // Optional full native flags for prepared authority maps. Older coarse
    // grids retain the byte flags; consumers share the same navigation rules.
    std::vector<uint16_t> fullTerrainCollision;
    struct Obstacle {
        EntityId id;
        int x, y, width, height;
        uint16_t mask;
        bool blocksLight = false;
        bool operator==(const Obstacle &) const = default;
    };
    std::vector<Obstacle> obstacles;
    std::vector<uint16_t> objectCollision;
    // Authority-owned COLLIDE_MISSILE bits. Separate from immutable DT1 flags
    // and object rasterization, which may be rebuilt while missiles are alive.
    std::vector<uint16_t> missileCollision; // Mutable native Set/ResetMask writes, including skill reservations.
    uint64_t missileCollisionRevision{};
    // Server room collision: original terrain, objects and committed unit bits.
    // Static-only geometry queries above keep their existing mask domain.
    std::vector<uint16_t> nativeCollision;
    uint64_t nativeCollisionRevision{};
    Bytes objectLightBlocked;
    uint64_t obstacleRevision = 0;
    // Borrow only direct walking neighbours after region storage is complete.
    // DS1's extra edge row is drawable terrain, not a duplicate collision room.
    struct BoundaryNeighbour {
        const Grid *grid;
        int offsetX, offsetY, side, plane, start, end;
    };
    std::vector<BoundaryNeighbour> neighbours;
    // Ephemeral read-only room lookup used by navigation, never authority or
    // imported terrain. Offsets convert a view cell to each owning grid.
    struct NavigationSource { const Grid *grid; int offsetX, offsetY; };
    std::vector<NavigationSource> navigationSources;
    // Ephemeral collision view; owns no terrain and must not survive a unit
    // publication. Coordinates remain local to this grid, including neighbours.
    std::function<uint16_t(int,int,EntityId)> collisionSample;
    // A path query withdraws/reinstalls footprints once, including nested path
    // helpers. Physical movement instead uses the transactional cell callback.
    std::function<void(bool)> collisionQuery;
    std::function<bool(int,int,int,int,MovementCollisionRule)> collisionStep;
    struct CollisionQueryScope {
        const Grid &grid;
        explicit CollisionQueryScope(const Grid &value):grid(value) {if(grid.collisionQuery) grid.collisionQuery(true);}
        ~CollisionQueryScope() {if(grid.collisionQuery) grid.collisionQuery(false);}
        CollisionQueryScope(const CollisionQueryScope &)=delete;
        CollisionQueryScope &operator=(const CollisionQueryScope &)=delete;
    };
    Grid() = default;
    Grid(int w, int h) : width(w), height(h), blocked(size_t(w) * h), lightBlocked(size_t(w) * h),
                         terrainCollision(size_t(w) * h), objectCollision(size_t(w) * h),
                         objectLightBlocked(size_t(w) * h) {}
    void setObstacles(std::vector<Obstacle> next);
    uint16_t objectMask(int x, int y, EntityId ignored = {}) const;
    uint16_t movementMask(int x, int y, EntityId ignored = {}) const;
    uint16_t nativeMovementMask(int x,int y,EntityId ignored={}) const;
    Grid linkedNavigation(Vec &origin) const;
    bool navigationContains(Vec point) const;
    uint64_t navigationRevision() const;
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
    // D2Common PATH_RayTrace uses integer cells and a biased major-axis walk.
    // Use together with segment() when a route is sent to the original server.
    bool nativeMovementSegment(Vec a, Vec b, MovementCollisionRule rule = playerMovement) const;
    Vec nativeMovementTarget(Vec a, Vec b, MovementCollisionRule rule = playerMovement) const;
    // Shared native Straight/Toward policy, with AStar fallback restricted to
    // 18 cells. Inputs/outputs use grid cell centers; callers own route state.
    std::deque<Vec> nativePlayerPath(Vec from, Vec to, MovementCollisionRule rule = playerMovement, uint8_t targetType = 255) const;
    std::deque<Vec> nativeAStarPath(Vec from, Vec to, MovementCollisionRule rule, bool unitTarget = false) const;
    // Native ordinary unit paths: AStar(1), Toward(2/13), Straight(7), WallFollow(15).
    // maxDistance bounds Toward's obstacle search, not its unobstructed ray.
    std::deque<Vec> nativeUnitPath(Vec from, Vec to, uint8_t type, uint8_t maxDistance, MovementCollisionRule rule, bool unitTarget = false, uint8_t targetTolerance = 1) const;
    std::deque<Vec> nativeWallFollowPath(Vec from, Vec to, uint8_t maxDistance, MovementCollisionRule rule) const;
    std::deque<Vec> nativeMonsterCirclePath(Vec from,Vec target,int distance,int turn,MovementCollisionRule rule) const;
    bool collisionSegment(Vec a, Vec b, uint16_t mask) const;
    // Object interaction uses the native shortened integer ray and flying
    // player mask; approaching it still uses the full walking footprint.
    bool interactionSegment(Vec a, Vec b, int targetSize, EntityId target,uint16_t mask=0x0804,int sourceSize=2) const;
    bool missileSegment(Vec a, Vec b, MissileCollisionRule rule) const;
    bool lightSegment(Vec a, Vec b) const;
    Bytes reachableFrom(Vec origin, MovementCollisionRule rule = {}) const;
    Vec nearest(Vec p, MovementCollisionRule rule = {}) const;
    // COLLISION_GetFreeCoordinatesImpl without a field mask: integer spawn,
    // square rings, Manhattan priority and native scan order; no unbounded fallback.
    std::optional<Vec> nativeSpawn(Vec p, int maxDistance, MovementCollisionRule rule) const;
    // Scene inspection arrival: an interior point of the largest walkable component.
    // Real level transitions must use the original linked warp coordinates instead.
    Vec inspectionArrival() const;
    // Partial routes are for movement requests; reachability checks stay exact.
    std::deque<Vec> path(Vec from, Vec to, bool allowPartial = false, MovementCollisionRule rule = {},
                         bool nativeSegments = false) const;
};
} // namespace d2x
