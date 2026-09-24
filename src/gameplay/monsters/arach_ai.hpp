#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class ArachAction { Approach, Attack, Web, Retreat, Circle, Idle };
ArachAction arachThink(Enemy &enemy, const MonsterAiProfile &rules,
                       float distance, bool inCombat);
} // namespace d2x
