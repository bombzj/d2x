#pragma once
#include "core/math.hpp"
#include <vector>
#include <optional>
#include <span>
#include <cstdint>
#include <algorithm>
#include <type_traits>

namespace d2x {
// Native velocity in 1/4096-cell units per frame, including integer level scaling.
std::optional<int> missileVelocityFixed(int base, int perLevel, int rank);
// PathMisc::sub_6FD5CEB0. completedTick is counted after the current native tick.
// Adapters retain their fixed-point or cells/second representation.
template<class Number> struct MissileVelocityStep { Number speed, acceleration; };
template<class Number> std::optional<MissileVelocityStep<Number>> advanceMissileVelocity(
    Number speed, Number acceleration, Number maximum, int completedTick) {
    static_assert(std::is_arithmetic_v<Number>);
    if (!acceleration || completedTick<=0 || completedTick%5!=0) return {};
    using Wide=std::conditional_t<std::is_integral_v<Number>,int64_t,Number>;
    const auto next=std::max(Wide(0),Wide(speed)+Wide(acceleration));
    if (next>=Wide(maximum)) return MissileVelocityStep<Number>{maximum,0};
    return MissileVelocityStep<Number>{Number(next),acceleration};
}
bool missileChangedCell(Vec previous, Vec next);
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames);
// Original 64-direction missile ring; shared by local authority and client effects.
Vec missileRingDirection(int index);
struct MissileRingEmission { Vec direction; int nextIndex{}; };
// SrvDo15 / CltDo19 use remaining frames, with separate table parameters.
bool missileEmissionDue(int remaining, int period);
std::optional<MissileRingEmission> missileRingEmission(int remaining, int period, int index, int step);
// SrvDo16 / CltDo20 only rotate inside the remaining-frame window.
std::optional<Vec> missileOrbTurn(Vec target, int remaining, int window, int period);
std::vector<Vec> missileRingBurst(int step);
Vec missileDiagonalTurn(Vec target);
// Native Fire Wall places makers perpendicular to the integer caster/aim ray.
Vec missileWallDirection(Vec caster, Vec target);
std::vector<Vec> missileFanTargets(Vec origin, Vec target, int count, Vec facing);
// Native GUID order: next larger eligible GUID, then wrap to the smallest.
uint64_t missileChainSuccessor(uint64_t hit, std::span<const uint64_t> eligible);
// Blizzard uses independent client/server random streams but the same seeded
// integer placement. Client offsets reverse the server's signed convention.
Vec blizzardOffset(uint32_t globalX, int remaining, int radius, bool client);
} // namespace d2x
