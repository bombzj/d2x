#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <optional>

namespace d2x {
struct Grid;
// Original D2Common_10399 distance, including both units' MPQ footprints.
int nativeUnitDistance(Vec first, int firstSize, Vec second, int secondSize);
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
