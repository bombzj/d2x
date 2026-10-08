#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/damage_type.hpp"

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
                                    const CharacterAttributes &defender);
float mitigateMonsterDamage(float amount, int resistance);
} // namespace d2x
