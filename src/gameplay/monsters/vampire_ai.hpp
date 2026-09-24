#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class VampireAction { Approach, Attack, CastFirst, CastFourth, Retreat, Circle, Idle };
VampireAction vampireThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool inCombat);
} // namespace d2x
