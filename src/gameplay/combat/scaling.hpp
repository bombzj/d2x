#pragma once
#include "gameplay/monsters/damage.hpp"

namespace d2x {
struct DamageUnitClass {
    bool player{}, hireling{}, revived{}, boss{}, primeEvil{};
};
// SUNITDMG_CalculateTotalDamage: classify the participants before any shield,
// flat reduction, resistance or absorb event. Duration fields are never scaled.
int damageClassPercentage(DamageUnitClass source, DamageUnitClass target,
                          int hirelingBossPercentage, bool sameUnit = false);
void scaleHitDamage(MonsterHit &hit, int percentage);
}
