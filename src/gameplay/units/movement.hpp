#pragma once
#include "core/math.hpp"
#include <deque>
#include <functional>
#include <optional>

namespace d2x {
// Synchronous collision capability; the caller supplies static/dynamic rules.
using MovementProbe = std::function<bool(Vec from, Vec to)>;
struct MovementStep {
    bool accepted = false;
    float distance = 0;
};
enum class RouteMovement { Moving, Arrived, Blocked };

void discardReachedWaypoints(Vec position, std::deque<Vec> &route, float tolerance);
// Direction and distance are already selected by the control policy. Optional
// clipping is used by direct movement only, never silently by routed actors.
MovementStep advanceMovement(Vec &position, Vec direction, float distance, const MovementProbe &probe,
                             int clipIterations = 0, std::optional<float> minimumDistance = std::nullopt);
RouteMovement advanceRouteMovement(Vec &position, std::deque<Vec> &route, float speed, float dt,
                                   float tolerance, const MovementProbe &probe);
} // namespace d2x
