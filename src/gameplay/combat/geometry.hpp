#pragma once
#include "core/math.hpp"
#include "world/collision.hpp"
#include <optional>

namespace d2x {
struct Grid;
// First blocked terrain/object contact. Coordinates may be world-relative;
// origin converts them to the prepared collision grid without changing ray arithmetic.
std::optional<float> missileTerrainContact(const Grid &, Vec from, Vec to, MissileCollisionRule, Vec origin = {});
int meleeDistance(Vec from, int fromSize, Vec to, int toSize);
bool playerMeleeReach(const Grid &, Vec from, Vec to, int targetSize, int rangeAdder);
bool unitMeleeReach(const Grid &,Vec from,int sourceSize,Vec to,int targetSize,int range,int rangeBonus=0);
int missileDistance(Vec from, Vec to);
Vec knockbackDestination(Vec target, Vec source, int distance);
std::optional<float> missileUnitIntersection(Vec from, Vec to, int missileSize, Vec unit, int unitSize);
} // namespace d2x
