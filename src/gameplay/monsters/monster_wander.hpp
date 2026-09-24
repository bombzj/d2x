#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
uint32_t monsterAiRandom(Enemy &enemy);
std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius);
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid);
} // namespace d2x
