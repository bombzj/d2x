#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance,
                   bool forcedByLevel);
} // namespace d2x
