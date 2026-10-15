#pragma once
#include "mitigation.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/damage_type.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
// Uncapped attribute lookup; mitigation applies its own original limits.
int rawResistance(const CharacterAttributes &attributes, DamageType type);
DamageMitigation damageMitigation(MonsterDamageType type, const CharacterAttributes &defender,
                                  bool monsterRules = false, int pierce = 0,
                                  bool bypass = false, bool sanctuary = false);
int damageResistance(int raw, int maximumBonus, bool monsterRules, bool physical = false, int pierce = 0);
// SkillSor::SKILLS_AuraCallback_StaticField, before the common damage stages.
// The difficulty minimum gates the callback; it is not a final damage clamp.
std::optional<int32_t> staticFieldDamageFixed(int32_t life,int32_t maximumLife,
    int damagePercent,int minimumPercent,int32_t minimumDamage,int resistance);
// Original resistance/pierce ordering shared by damage and duration channels.
int piercedResistance(int resistance, int pierce);
uint64_t resistedDuration(uint64_t frames, int resistance);
uint64_t coldHitDuration(uint64_t frames, int resistance, int pierce,
                         bool cannotBeFrozen, bool halfFreezeDuration);
} // namespace d2x
