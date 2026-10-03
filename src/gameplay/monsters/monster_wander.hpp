#pragma once
#include "core/math.hpp"
#include "world/navigation.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
struct Enemy;
uint32_t monsterAiRandom(Enemy &enemy);
int monsterAiDistance(Vec from, int size, Vec target);
void monsterStartApproach(Enemy &enemy, int stopDistance, int velocityPercent, bool running,
                          std::optional<Vec> destination = std::nullopt);
void monsterStopApproach(Enemy &enemy);
Vec monsterRadiusApproachTarget(Vec from, int size, Vec target, int radius, int targetDistance = 0);
std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius, MovementCollisionRule rule);
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule);
bool monsterStartCircle(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule);
void monsterAdvanceCircle(Enemy &enemy, const Grid &grid, float speed, float dt, MovementCollisionRule rule);
} // namespace d2x
