#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class BloodHawkAction { Approach, Charge, Attack, Retreat, Circle };
BloodHawkAction bloodHawkThink(Enemy &enemy, const MonsterAiProfile &rules,
                               float distance, bool inCombat);
} // namespace d2x
