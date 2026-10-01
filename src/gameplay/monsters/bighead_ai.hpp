#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class BigheadAction { Melee, Fire, Approach, Circle, Retreat, Idle };
BigheadAction bigheadThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool clear, bool inCombat);
} // namespace d2x
