#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy);
int bruteAttackMode(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
