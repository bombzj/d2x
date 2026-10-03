#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class FetishAction { Approach, Attack, Retreat, Circle, Idle };
FetishAction fetishThink(Enemy &enemy, const MonsterAiProfile &rules,
                         float distance, bool inCombat, int targetLifePercent);
void fetishRetreatFailed(Enemy &enemy);
} // namespace d2x
