#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
bool corruptArcherRetreats(Enemy &enemy, const MonsterAiProfile &rules);
bool corruptArcherApproaches(Enemy &enemy, const MonsterAiProfile &rules, float distance);
bool corruptArcherShoots(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
