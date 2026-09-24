#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"

namespace d2x {
enum class FallenMovement { Approach, Wander, Idle };
FallenMovement fallenMovement(Enemy &enemy, const MonsterAiProfile &rules, float distance);
bool fallenAttacks(Enemy &enemy, const MonsterAiProfile &rules);
bool fallenStartEscape(Enemy &enemy, Vec player, const Grid &grid);
void fallenAdvanceEscape(Enemy &enemy, const Grid &grid, float speed, float dt);
} // namespace d2x
