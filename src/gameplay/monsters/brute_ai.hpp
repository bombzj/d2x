#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy);
bool bruteAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
