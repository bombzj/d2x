#pragma once
#include "gameplay/combat/weapon_attack.hpp"
#include <functional>
namespace d2x {
struct RuntimeCombatUnit;
// Shield block precedes avoidance. Returns the original passive skill ID.
int avoidCombatHit(RuntimeCombatUnit target, bool ranged,
                   const std::function<std::optional<WeaponAttackTiming>()> &animation);
} // namespace d2x
