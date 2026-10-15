#pragma once
#include "weapon_timing.hpp"
#include <vector>

namespace d2x {
struct WeaponVolley { std::vector<int> frames, hits; };
WeaponVolley weaponVolley(const WeaponAttackTiming &timing, int count, int rollback);
int strafeShotCount(int targets, int maximum, int minimum);
}