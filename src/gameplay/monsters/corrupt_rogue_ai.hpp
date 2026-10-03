#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class CorruptRogueMovement { Walk, Run, Idle };
CorruptRogueMovement corruptRogueMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                         float distance, int difficulty);
bool corruptRogueAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
