#pragma once
#include "aura_spec.hpp"
#include <cstdint>
#include <map>
#include <optional>

namespace d2x {
struct SkillFormulaCatalog;
struct SkillFormulaInput;
// Original SKILL_ComputePeriodicRate: clamp PerDelay to five, round the
// current frame upward to a period boundary, then add one. Signed x86 math.
int32_t skillPeriodicFrame(int32_t frame, int32_t delay);
int32_t skillPeriodicDuration(int32_t frame, int32_t delay);
// Timed-event adapter: preserve the native signed due-frame comparison.
// The -1 sentinel first executes on the next frame; item callbacks cancel
// and replace their source's event after every execution.
uint64_t skillPeriodicTick(uint64_t tick, int32_t delay);
int32_t nativeAuraDirectStatValue(int32_t current,int32_t bonus,std::optional<int32_t> maximum);
uint64_t nativeAuraDurationTick(uint64_t tick,uint64_t until,int remainingPercent);
// Source adapters supply ranks and bonuses; evaluation never queries an actor.
AuraDefinition evaluateBaseAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks = {}, int prayerRank = 0);
AuraDefinition evaluateAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks = {}, int fireMasteryPercent = 0,
    int lightningMasteryPercent = 0, int coldDamagePercent = 0, int prayerRank = 0);
AuraDefinition evaluateNativeAura(const AuraSkillSpec &,int rank,const SkillFormulaCatalog &,const SkillFormulaInput &);
} // namespace d2x
