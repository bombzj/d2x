#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class CorruptLancerMovement { Walk, Run, Idle };
CorruptLancerMovement corruptLancerMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                           float distance);
bool corruptLancerAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
