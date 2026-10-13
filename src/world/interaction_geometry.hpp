#pragma once
#include "core/id.hpp"
#include "core/math.hpp"

namespace d2x {
struct Grid;
// Original D2Common_10399 distance, including both units' MPQ footprints.
int nativeUnitDistance(Vec first, int firstSize, Vec second, int secondSize);
bool nativePickupReach(const Grid &grid, Vec player, Vec item, EntityId id);
struct InteractionTarget {
    EntityId id;
    Vec pos, accessPoint;
    int collisionWidth = 0, collisionHeight = 0;
    float reach = 0;
    bool nativeObject = false;
    bool nativeUnit = false;
    bool collisionRequired = true;
};
bool interactionClear(const Grid &grid, Vec position, const InteractionTarget &target);
} // namespace d2x
