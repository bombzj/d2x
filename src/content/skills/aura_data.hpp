#pragma once
#include "gameplay/skills/aura_resolve.hpp"
#include <optional>

namespace d2x {
struct ClassicData;
void loadAuraSkills(ClassicData &data);
// Content boundary; source-specific callers choose base or bonus evaluation.
AuraDefinition resolveBaseAura(const ClassicData &data, int skill, int rank);
std::optional<AuraDefinition> resolveAura(const ClassicData &data, int skill, int rank,
    const std::map<int, int> &ranks = {}, int fireMasteryPercent = 0,
    int lightningMasteryPercent = 0, int coldDamagePercent = 0, int prayerRank = 0);
} // namespace d2x
