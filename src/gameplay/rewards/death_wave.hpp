#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace d2x {
struct DeathWaveTarget { EntityId id; Vec position; bool eligible = false; };
struct DeathWaveDelay { uint32_t base = 0, randomBound = 0; };
struct DelayedQuestDeath { EntityId target; uint64_t frame = 0; };
struct DeathWavePlan { uint64_t random = 0; std::vector<DelayedQuestDeath> deaths; };
// Callers resolve living/allied/near-room/undead qualification. The pure rule
// keeps original integer position truncation and ordered random consumption.
DeathWavePlan planDeathWave(Vec origin, uint64_t frame, DeathWaveDelay delay,
    std::span<const DeathWaveTarget> targets, uint64_t random);
} // namespace d2x
