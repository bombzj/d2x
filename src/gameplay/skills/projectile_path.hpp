#pragma once
#include "core/math.hpp"
#include <vector>
#include <optional>

namespace d2x {
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames);
// Original 64-direction missile ring; shared by local authority and client effects.
Vec missileRingDirection(int index);
struct MissileRingEmission { Vec direction; int nextIndex{}; };
// The adapter chooses elapsed or remaining frame. Sharing math must not silently
// replace client visual clocks with authority clocks (or vice versa).
std::optional<MissileRingEmission> missileRingEmission(int phaseFrame, int period, int index, int step);
std::vector<Vec> missileRingBurst(int step);
Vec missileDiagonalTurn(Vec target);
std::vector<Vec> missileFanTargets(Vec origin, Vec target, int count, Vec facing);
} // namespace d2x
