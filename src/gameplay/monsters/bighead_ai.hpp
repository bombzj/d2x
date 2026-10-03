#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class BigheadAction { Melee, Fire, Approach, Circle, Retreat, Idle };
BigheadAction bigheadThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool clear, bool inCombat);
} // namespace d2x
