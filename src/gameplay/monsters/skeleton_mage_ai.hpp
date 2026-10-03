#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class SkeletonMageAction { Fire, Approach, Retreat, Circle, Idle };
SkeletonMageAction skeletonMageThink(Enemy &enemy, const MonsterAiProfile &rules,
                                     float distance, bool clear);
} // namespace d2x
