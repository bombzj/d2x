#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class BloodHawkAction { Approach, Charge, Attack, Retreat, Circle };
BloodHawkAction bloodHawkThink(Enemy &enemy, const MonsterAiProfile &rules,
                               float distance, bool inCombat);
} // namespace d2x
