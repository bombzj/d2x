#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class FetishAction { Approach, Attack, Retreat, Circle, Idle };
FetishAction fetishThink(Enemy &enemy, const MonsterAiProfile &rules,
                         float distance, bool inCombat, int targetLifePercent);
void fetishRetreatFailed(Enemy &enemy);
} // namespace d2x
