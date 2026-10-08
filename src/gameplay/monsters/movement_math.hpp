#pragma once
#include "core/math.hpp"
#include <cstdint>
#include <array>
namespace d2x {
// Master monster_wander geometry, separated from Enemy and Grid ownership.
int monsterAiDistance(Vec from, int size, Vec target);
Vec monsterWanderPoint(Vec from, int radius, uint64_t &random);
Vec monsterRetreatPoint(Vec from, Vec target, int distance);
std::array<Vec,3> monsterCirclePoints(Vec from, Vec target, int distance, uint64_t &random);
Vec monsterRadiusApproachPoint(Vec from, int size, Vec target, int radius, int targetDistance = 0);
}
