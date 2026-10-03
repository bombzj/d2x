#pragma once
#include "gameplay/skills/summon_spec.hpp"

namespace d2x {
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty);
} // namespace d2x
