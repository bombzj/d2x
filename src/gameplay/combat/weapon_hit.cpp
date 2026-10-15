#include "weapon_hit.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "drain.hpp"
#include <algorithm>

namespace d2x {
int32_t nativeThornsDamage(int32_t physical,int32_t percent,bool playerOrHirelingAttacker) {
    if(percent<=0) return 0;
    if(playerOrHirelingAttacker) percent=nativeDivide(nativeAdd(percent,4),8);
    return percent>0?nativePercentage(physical,percent,100):0;
}
namespace {
int64_t leechedResource(int64_t physical, int percent, int drain, int divisor) {
    if (physical <= 0 || percent <= 0 || drain <= 0 || divisor <= 0) return 0;
    // SUNITDMG_ExecuteEvents retains six fractional bits in the percentage
    // until after difficulty, physical damage and MonStats Drain scaling.
    const auto scaled = nativeDivide(nativeShiftLeft(percent, 6), divisor);
    return nativeLeechFromScaledPercent(nativeChecked(physical),scaled,drain);
}
int64_t openWoundsRate(int level) {
    int64_t rate = 40;
    int remaining = std::max(0, level - 1);
    constexpr int increments[]{9, 18, 27, 36, 45};
    for (int tier = 0; tier < 5 && remaining; ++tier) {
        const int levels = tier == 4 ? remaining : std::min(remaining, tier == 0 ? 14 : 15);
        rate += int64_t(levels) * increments[tier];
        remaining -= levels;
    }
    return rate;
}
}
WeaponHitResult calculateWeaponHitEffects(const WeaponSkillDamage &attack, const WeaponHitTarget &target,
                                         int64_t physical, LeechDivisors divisors, uint64_t &random) {
    WeaponHitResult result;
    // Item events are rolled at each successful contact, not once per volley.
    if (attack.crushingBlow > 0 && limitedRandom(random, 100) < unsigned(attack.crushingBlow)) {
        int divisor = target.player || target.hireling ? 10 : target.boss ? 8 : 4;
        if (!target.player && !target.hireling) divisor=nativeAdd(divisor,nativePercentage(divisor,nativeMultiply(nativeChecked(std::max(1u,target.playerCount)-1),50),100));
        if (attack.projectile) divisor=nativeMultiply(divisor,2);
        result.crushing = std::max<int64_t>(0, target.life) / divisor;
        const auto crushing = nativeChecked(result.crushing);
        result.crushing = nativeSubtract(crushing, nativePercentage(crushing,
            std::clamp(target.physicalResistance, 0, 100), 100));
    }
    const int64_t dealt = std::min(std::max<int64_t>(0, target.life - result.crushing), std::max<int64_t>(0, physical));
    result.life = leechedResource(dealt, attack.lifeLeech, target.drain, divisors.life);
    result.mana = leechedResource(dealt, attack.manaLeech, target.drain, divisors.mana);
    if (attack.openWounds > 0 && limitedRandom(random, 100) < unsigned(attack.openWounds))
        result.woundRate = openWoundsRate(attack.level) / (target.player ? (attack.projectile ? 8 : 4) : target.reducedWounds ? 2 : 1);
    return result;
}
}
