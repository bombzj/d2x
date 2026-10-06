#pragma once
#include "core/math.hpp"
#include <vector>

namespace d2x {
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames);
// Original 64-direction missile ring; shared by local authority and client effects.
Vec missileRingDirection(int index);
std::vector<Vec> missileFanTargets(Vec origin, Vec target, int count, Vec facing);
} // namespace d2x
