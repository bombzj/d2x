#pragma once
#include "core/math.hpp"
#include "gameplay/monsters/rank.hpp"

namespace d2x {
enum class SkillAreaCoordinates { TruncatedCells, FlooredCells, Continuous };
// Callers retain relation, room, collision and target ordering rules.
bool withinSkillArea(Vec origin, Vec target, float radius, SkillAreaCoordinates coordinates);
bool skillAiControlAllowed(MonsterRank rank, bool switchAi, bool boss);
}
