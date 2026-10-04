#pragma once
#include "core/id.hpp"
#include <vector>
namespace d2x {
struct Missile;
struct CombatModifiers;
struct MissileLaunchSpec { int velocityPercent = 100, pierceChance = 0; };
struct MissilePierceState { int remaining = 0; std::vector<EntityId> hits; };
void prepareMissileLaunch(Missile &, const CombatModifiers &, bool canSlow, bool canPierce);
bool consumeMissilePierce(Missile &, EntityId target);
bool missileAlreadyHit(const Missile &, EntityId target);
} // namespace d2x
