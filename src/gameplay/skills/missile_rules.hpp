#pragma once
#include "gameplay/combat/damage_type.hpp"

namespace d2x {
struct Missile;
DamageType missileElement(const Missile &missile);
bool missilePierces(const Missile &missile);
bool missileFollowsPath(const Missile &missile);
} // namespace d2x
