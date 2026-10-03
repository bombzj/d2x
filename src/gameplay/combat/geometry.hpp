#pragma once
#include "core/math.hpp"
#include <optional>

namespace d2x {
int meleeDistance(Vec from, int fromSize, Vec to, int toSize);
int missileDistance(Vec from, Vec to);
std::optional<float> missileUnitIntersection(Vec from, Vec to, int missileSize, Vec unit, int unitSize);
} // namespace d2x
