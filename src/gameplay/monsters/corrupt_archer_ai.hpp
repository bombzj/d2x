#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
bool corruptArcherRetreats(Enemy &enemy, const MonsterAiProfile &rules);
bool corruptArcherApproaches(Enemy &enemy, const MonsterAiProfile &rules, float distance);
bool corruptArcherShoots(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
