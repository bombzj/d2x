#include "interaction_geometry.hpp"
#include "world/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
bool originalObject(const InteractionTarget &object) {
    return object.nativeObject;
}
// D2MOO Units.cpp: D2Common_10399 / UNITS_IsObjectInInteractRange
// (MIT; docs/licenses/D2MOO.txt). The server does not use OperateRange as
// a center-to-center radius. Player SizeX/SizeY are both 2.
bool objectInRange(Vec position, const InteractionTarget &object) {
    const int x = int(std::floor(position.x)), y = int(std::floor(position.y));
    const int ox = int(std::floor(object.pos.x)), oy = int(std::floor(object.pos.y));
    const int dx = std::abs(x - ox), dy = std::abs(y - oy);
    const int width = object.collisionWidth, height = object.collisionHeight;
    if (dx >= 8 || dy >= 8 || width >= 4) {
        const int size = 1 + width / 2;
        if (dx <= size && dy <= size) return true;
    } else {
        constexpr int distances[64]{
            -1,-1,-1,0,2,4,6,8, -1,-1,0,1,2,4,6,8,
            -1,0,0,2,3,5,7,8, 0,1,2,2,4,5,7,8,
            2,2,3,4,5,6,7,9, 4,4,5,5,6,7,8,9,
            6,6,7,7,7,8,10,10, 8,8,8,8,9,9,10,11
        };
        int distance = distances[dx + 8 * dy];
        if (distance < 0) return true;
        if (width == 3) distance = std::max(0, distance - 1);
        if (width <= 1) ++distance;
        if (!distance) return true;
    }
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
bool interactionClear(const Grid &grid, Vec position, const InteractionTarget &object) {
    if (originalObject(object))
        return objectInRange(position, object) &&
            grid.interactionSegment(position, object.pos, object.collisionWidth, object.id);
    return (position - object.pos).length() <= object.reach &&
        grid.segment(position, interactionPoint(grid, object), object.id);
}
std::optional<Vec> interactionApproach(const Grid &grid, Vec from, const InteractionTarget &object) {
    std::optional<Vec> best;
    float bestCost = std::numeric_limits<float>::infinity();
    const int radius = originalObject(object)
        ? std::max(object.collisionWidth, object.collisionHeight) / 2 + 3
        : int(std::ceil(object.reach));
    const int centerX = int(std::floor(object.pos.x)), centerY = int(std::floor(object.pos.y));
    for (int y = centerY - radius; y <= centerY + radius; ++y)
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            if (!grid.walkable(x, y, playerMovement))
                continue;
            Vec candidate{x + .5f, y + .5f};
            if (!interactionClear(grid, candidate, object))
                continue;
            if ((candidate - from).length() >= bestCost)
                continue;
            auto path = grid.path(from, candidate, false, playerMovement);
            if (path.empty() && (from - candidate).length() > .01f)
                continue;
            float cost = 0;
            Vec previous = from;
            for (auto step : path) {
                cost += (step - previous).length();
                previous = step;
            }
            if (cost < bestCost) {
                best = candidate;
                bestCost = cost;
            }
        }
    return best;
}
} // namespace d2x
