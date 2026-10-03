#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class BruteCombat { Attack, Circle, Idle };
float bruteWalkMultiplier(const Enemy &enemy);
BruteCombat bruteCombat(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
