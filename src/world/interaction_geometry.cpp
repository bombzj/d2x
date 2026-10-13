#include "interaction_geometry.hpp"
#include "world/navigation.hpp"
#include "world/native_target_path.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
// D2MOO Units.cpp::D2Common_10399 (MIT; docs/licenses/D2MOO.txt).
// Shared by native object checks and server-confirmed NPC approach checks.
int nativeUnitDistance(Vec first, int firstSize, Vec second, int secondSize) {
    // Vec projects native precise feet with the drawing half-cell removed.
    // Restore that offset before reading the integer subtile, just as the
    // movement pattern, room ownership and native wire projection do.
    const int dx = std::abs(int(std::floor(first.x+.5f)) - int(std::floor(second.x+.5f)));
    const int dy = std::abs(int(std::floor(first.y+.5f)) - int(std::floor(second.y+.5f)));
    if (dx >= 8 || dy >= 8 || firstSize >= 4 || secondSize >= 4) {
        const int size = firstSize / 2 + secondSize / 2;
        const int x = std::max(0, dx - size), y = std::max(0, dy - size);
        return std::min(x, y) + 2 * std::max(x, y);
    }
    constexpr int distances[64]{
        -1,-1,-1,0,2,4,6,8, -1,-1,0,1,2,4,6,8,
        -1,0,0,2,3,5,7,8, 0,1,2,2,4,5,7,8,
        2,2,3,4,5,6,7,9, 4,4,5,5,6,7,8,9,
        6,6,7,7,7,8,10,10, 8,8,8,8,9,9,10,11
    };
    int distance = distances[dx + 8 * dy];
    if (distance < 0) return 0;
    if (firstSize == 3 || secondSize == 3) distance = std::max(0, distance - 1);
    if (firstSize <= 1 || secondSize <= 1) ++distance;
    return distance;
}
namespace {
bool originalObject(const InteractionTarget &object) {
    return object.nativeObject;
}
// D2MOO Units.cpp: D2Common_10399 / UNITS_IsObjectInInteractRange
// (MIT; docs/licenses/D2MOO.txt). The server does not use OperateRange as
// a center-to-center radius. Player SizeX/SizeY are both 2.
bool objectInRange(Vec position, const InteractionTarget &object) {
    const int x = int(std::floor(position.x+.5f)), y = int(std::floor(position.y+.5f));
    const int ox = int(std::floor(object.pos.x+.5f)), oy = int(std::floor(object.pos.y+.5f));
    const int dx = std::abs(x - ox), dy = std::abs(y - oy);
    const int width = object.collisionWidth, height = object.collisionHeight;
    if (dx >= 8 || dy >= 8 || width >= 4) {
        const int size = 1 + width / 2;
        if (dx <= size && dy <= size) return true;
    } else if (!nativeUnitDistance(position, 2, object.pos, width)) return true;
    const int left = ox - width / 2, top = oy - height / 2;
    if (width < 1 || height < 1)
        return x >= left - 1 && x <= left + 1 && y >= top - 1 && y <= top + 1;
    if (x < left - 2 || x > left + width + 2 || y < top - 2 || y > top + height + 2)
        return false;
    // A two-subtile player may stand in either side strip, but not the
    // outer corner squares of the expanded object rectangle.
    return (y >= top - 1 && y <= top + height + 1) ||
           (x >= left - 1 && x <= left + width + 1);
}
Vec interactionPoint(const Grid &grid, const InteractionTarget &object) {
    // Some authored targets sit on a blocked terrain tile. Preserve their
    // external access point; ignore only the target object's own footprint.
    return grid.segment(object.pos, object.pos, object.id) ? object.pos : object.accessPoint;
}
} // namespace
bool nativePickupReach(const Grid &grid, Vec player, Vec item, EntityId id) {
    return nativeUnitDistance(player,2,item,1)<=4 && grid.interactionSegment(player,item,1,id);
}
bool interactionClear(const Grid &grid, Vec position, const InteractionTarget &object) {
    if (object.nativeUnit)
        return nativeUnitDistance(position,2,object.pos,object.collisionWidth)<=object.reach &&
            (!object.collisionRequired || grid.interactionSegment(position,object.pos,object.collisionWidth,object.id));
    if (originalObject(object))
        return objectInRange(position, object) &&
            grid.interactionSegment(position, object.pos, object.collisionWidth, object.id);
    return (position - object.pos).length() <= object.reach &&
        grid.segment(position, interactionPoint(grid, object), object.id);
}
std::deque<Vec> nativeTargetPath(const Grid &grid, Vec position, const InteractionTarget &target, uint8_t targetType) {
    if (interactionClear(grid, position, target)) return {};
    // PathMisc::sub_6FD5DB70 / D2Common_10236: object and unreached living
    // unit targets recompute at a partial endpoint. Straight stays Straight;
    // this is not an unconditional switch to WallFollow or an unbounded solver.
    constexpr Vec center{.5f, .5f};
    auto route = grid.nativePlayerPath(position + center, target.pos + center, playerMovement, targetType);
    for (auto &point : route) point = point - center;
    return route;
}
} // namespace d2x
