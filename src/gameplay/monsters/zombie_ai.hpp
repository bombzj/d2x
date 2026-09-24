#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance);
} // namespace d2x
