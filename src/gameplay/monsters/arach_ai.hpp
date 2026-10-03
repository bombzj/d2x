#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class ArachAction { Approach, Attack, Web, Retreat, Circle, Wander, Idle };
ArachAction arachThink(Enemy &enemy, const MonsterAiProfile &rules,
                       float distance, bool inCombat);
} // namespace d2x
