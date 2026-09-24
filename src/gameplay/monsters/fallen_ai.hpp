#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class FallenMovement { Approach, Wander, Idle };
FallenMovement fallenMovement(Enemy &enemy, const MonsterAiProfile &rules, float distance);
bool fallenAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
