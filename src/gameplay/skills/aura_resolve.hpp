#pragma once
#include "aura_spec.hpp"
#include <map>

namespace d2x {
// Source adapters supply ranks and bonuses; evaluation never queries an actor.
AuraDefinition evaluateBaseAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks = {}, int prayerRank = 0);
AuraDefinition evaluateAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks = {}, int fireMasteryPercent = 0,
    int lightningMasteryPercent = 0, int coldDamagePercent = 0, int prayerRank = 0);
} // namespace d2x
