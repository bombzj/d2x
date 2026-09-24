#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"

namespace d2x {
enum class BruteCombat { Attack, Circle, Idle };
float bruteWalkMultiplier(const Enemy &enemy);
BruteCombat bruteCombat(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
