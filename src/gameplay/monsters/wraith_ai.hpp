#pragma once
#include "gameplay/model/state.hpp"
#include "gameplay/monsters/monster_spawn.hpp"

namespace d2x {
bool wraithApproaches(Enemy &enemy, const MonsterAiProfile &rules);
bool wraithAttacks(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
