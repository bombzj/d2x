#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include <optional>

namespace d2x {
bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance);
std::optional<Vec> zombieWanderTarget(Enemy &enemy, const Grid &grid);
} // namespace d2x
