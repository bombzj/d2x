#include "gameplay/units/movement.hpp"
#include <algorithm>

namespace d2x {
void discardReachedWaypoints(Vec position, std::deque<Vec> &route, float tolerance) {
    while (!route.empty() && (route.front() - position).length() < tolerance) route.pop_front();
}
MovementStep advanceMovement(Vec &position, Vec direction, float distance, const MovementProbe &probe,
                             int clipIterations, std::optional<float> minimumDistance) {
    Vec next = position + direction * distance;
    if (clipIterations > 0 && (!minimumDistance || distance > *minimumDistance) && !probe(position, next)) {
        float clear = 0, blocked = distance;
        for (int iteration = 0; iteration < clipIterations; ++iteration) {
            const float middle = (clear + blocked) * .5f;
            if (probe(position, position + direction * middle)) clear = middle;
            else blocked = middle;
        }
        distance = clear;
        next = position + direction * distance;
    }
    const bool accepted = (!minimumDistance || distance > *minimumDistance) && probe(position, next);
    if (accepted) position = next;
    return {accepted, distance};
}
RouteMovement advanceRouteMovement(Vec &position, std::deque<Vec> &route, float speed, float dt,
                                   float tolerance, const MovementProbe &probe) {
    discardReachedWaypoints(position, route, tolerance);
    if (route.empty()) return RouteMovement::Arrived;
    const Vec offset = route.front() - position;
    if (!advanceMovement(position, offset.unit(), std::min(speed * dt, offset.length()), probe).accepted) {
        route.clear();
        return RouteMovement::Blocked;
    }
    if ((route.front() - position).length() < tolerance) route.pop_front();
    return route.empty() ? RouteMovement::Arrived : RouteMovement::Moving;
}
} // namespace d2x
