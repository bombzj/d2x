#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class CorruptRogueMovement { Walk, Run, Idle };
CorruptRogueMovement corruptRogueMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                         float distance, int difficulty);
bool corruptRogueAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
