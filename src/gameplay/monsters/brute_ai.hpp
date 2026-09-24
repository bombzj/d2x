#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"

namespace d2x {
enum class BruteCombat { Attack, Circle, Idle };
float bruteWalkMultiplier(const Enemy &enemy);
BruteCombat bruteCombat(Enemy &enemy, const MonsterAiProfile &rules);
bool bruteStartCircle(Enemy &enemy, Vec target, const Grid &grid);
void bruteAdvanceCircle(Enemy &enemy, const Grid &grid, float speed, float dt);
} // namespace d2x
