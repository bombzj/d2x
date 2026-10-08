#pragma once
#include "core/math.hpp"
#include <cstdint>
#include <vector>
#include <utility>
namespace d2x {
// MonsterMode extra quills: caller supplies its own SEIS stream, both sides
// keep the original persistent sign flips and five-cell offsets.
std::vector<Vec> monsterQuillTargets(Vec target, int count, uint64_t &random);
int monsterFacing8(Vec from,Vec to);
Vec monsterDirectionOffset(int index);
std::pair<Vec,Vec> monsterWebTrailPoints(Vec position,Vec movement);
}
