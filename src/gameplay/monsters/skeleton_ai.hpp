#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
bool skeletonApproaches(Enemy &enemy, const MonsterAiProfile &rules);
bool skeletonAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
