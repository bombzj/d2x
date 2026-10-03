#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class SkeletonBowAction { Shoot, Approach, Circle, Idle };
SkeletonBowAction skeletonBowThink(Enemy &enemy, const MonsterAiProfile &rules,
                                   float distance, bool clear);
} // namespace d2x
