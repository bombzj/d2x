#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
bool goatmanApproaches(Enemy &enemy, const MonsterAiProfile &rules);
bool goatmanAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
