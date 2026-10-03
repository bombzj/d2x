#pragma once
#include "gameplay/skills/cast_spec.hpp"
#include <map>

namespace d2x {
struct SkillSpec;
// Borrowed only for the duration of evaluation. Prepared by the authority-side
// source adapter; this is neither an actor state nor a client command payload.
struct SkillEvaluationInput {
    int rank;
    const std::map<int, int> &synergyRanks;
    int fireMasteryPercent = 0, lightningMasteryPercent = 0, coldDamagePercent = 0;
};
SkillCastSpec resolveSkill(const SkillSpec &spec, const SkillEvaluationInput &input);
} // namespace d2x
