#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
bool skeletonApproaches(Enemy &enemy, const MonsterAiProfile &rules);
bool skeletonAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
