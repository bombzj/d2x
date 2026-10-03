#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class CorruptLancerMovement { Walk, Run, Idle };
CorruptLancerMovement corruptLancerMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                           float distance);
bool corruptLancerAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
