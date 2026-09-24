#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class SkeletonBowAction { Shoot, Approach, Idle };
SkeletonBowAction skeletonBowThink(Enemy &enemy, const MonsterAiProfile &rules,
                                   float distance, bool clear);
} // namespace d2x
