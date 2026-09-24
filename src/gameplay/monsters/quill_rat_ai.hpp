#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"

namespace d2x {
bool quillRatShoots(Enemy &enemy, const MonsterAiProfile &rules);
bool quillRatStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid);
} // namespace d2x
