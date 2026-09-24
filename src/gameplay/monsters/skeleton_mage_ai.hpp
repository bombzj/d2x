#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class SkeletonMageAction { Fire, Approach, Retreat, Circle, Idle };
SkeletonMageAction skeletonMageThink(Enemy &enemy, const MonsterAiProfile &rules,
                                     float distance, bool clear);
} // namespace d2x
