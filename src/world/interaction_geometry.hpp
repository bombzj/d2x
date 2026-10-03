#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <optional>

namespace d2x {
struct Grid;
struct InteractionTarget {
    EntityId id;
    Vec pos, accessPoint;
    int collisionWidth = 0, collisionHeight = 0;
    float reach = 0;
    bool nativeObject = false;
};
bool interactionClear(const Grid &grid, Vec position, const InteractionTarget &target);
std::optional<Vec> interactionApproach(const Grid &grid, Vec from, const InteractionTarget &target);
} // namespace d2x
