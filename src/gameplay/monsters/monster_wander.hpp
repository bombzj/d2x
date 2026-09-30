#pragma once
#include "gameplay/model/state.hpp"
#include "world/navigation.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
uint32_t monsterAiRandom(Enemy &enemy);
std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius, MovementCollisionRule rule);
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule);
bool monsterStartCircle(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule);
void monsterAdvanceCircle(Enemy &enemy, const Grid &grid, float speed, float dt, MovementCollisionRule rule);
} // namespace d2x
