#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/damage_type.hpp"
#include <cstdint>

namespace d2x {
// Uncapped attribute lookup; mitigation applies its own original limits.
int rawResistance(const CharacterAttributes &attributes, DamageType type);
struct ResolvedDamage {
    float dealt = 0;
    float absorbed = 0;
};
// Damage is converted to the original 8-bit fixed-point HP unit before
// flat reduction, resistance and absorption (SUnitDmg.cpp).
ResolvedDamage mitigatePlayerDamage(float amount, MonsterDamageType type,
                                    const CharacterAttributes &defender, bool monsterRules = false);
float mitigateMonsterDamage(float amount, int resistance);
// Original resistance/pierce ordering shared by damage and duration channels.
int piercedResistance(int resistance, int pierce);
uint64_t resistedDuration(uint64_t frames, int resistance);
uint64_t coldHitDuration(uint64_t frames, int resistance, int pierce,
                         bool cannotBeFrozen, bool halfFreezeDuration);
} // namespace d2x
