#pragma once
#include <cstdint>

namespace d2x {
int32_t nativeThornsDamage(int32_t physical,int32_t percent,bool playerOrHirelingAttacker);
struct WeaponSkillDamage;
struct WeaponHitTarget {
    int64_t life{};
    int physicalResistance{}, drain{};
    unsigned playerCount{1};
    bool hireling{}, boss{}, reducedWounds{};
    bool player{};
};
struct LeechDivisors { int life{1}, mana{1}; };
struct WeaponHitResult { int64_t crushing{}, life{}, mana{}, woundRate{}; };
// Physical damage is already mitigated. Crushing Blow is separate from the
// physical channel used for leech, and precedes its remaining-life cap.
WeaponHitResult calculateWeaponHitEffects(const WeaponSkillDamage &, const WeaponHitTarget &,
                                         int64_t physical, LeechDivisors, uint64_t &random);
}
