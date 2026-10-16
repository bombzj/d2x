#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include "gameplay/monsters/rank.hpp"
#include <cstdint>
#include <span>

namespace d2x {
enum class SkillAreaCoordinates { TruncatedCells, FlooredCells, Continuous };
// Callers retain relation, room, collision and target ordering rules.
bool withinSkillArea(Vec origin, Vec target, float radius, SkillAreaCoordinates coordinates);
bool withinNativeAuraArea(Vec origin,Vec target,int32_t radius);
bool skillAiControlAllowed(MonsterRank rank, bool switchAi, bool boss);
EntityId nativeSkillSuccessor(std::span<const EntityId>,uint32_t previous);
}
