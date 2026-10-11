#include "weapon_hit.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
namespace {
int64_t leechedResource(int64_t physical, int percent, int drain, int divisor) {
    if (physical <= 0 || percent <= 0 || drain <= 0 || divisor <= 0) return 0;
    // SUNITDMG_ExecuteEvents retains six fractional bits in the percentage
    // until after difficulty, physical damage and MonStats Drain scaling.
    const int64_t scaled = int64_t(percent) * 64 / divisor;
    return physical * scaled / 100 * drain / 100 / 64;
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
        int divisor = target.hireling ? 10 : target.boss ? 8 : 4;
        if (!target.hireling) divisor += divisor * int(std::max(1u, target.playerCount) - 1) / 2;
        if (attack.projectile) divisor *= 2;
        result.crushing = std::max<int64_t>(0, target.life) / divisor;
        result.crushing -= result.crushing * std::clamp(target.physicalResistance, 0, 100) / 100;
    }
    const int64_t dealt = std::min(std::max<int64_t>(0, target.life - result.crushing), std::max<int64_t>(0, physical));
    result.life = leechedResource(dealt, attack.lifeLeech, target.drain, divisors.life);
    result.mana = leechedResource(dealt, attack.manaLeech, target.drain, divisors.mana);
    if (attack.openWounds > 0 && limitedRandom(random, 100) < unsigned(attack.openWounds))
        result.woundRate = openWoundsRate(attack.level) / (target.reducedWounds ? 2 : 1);
    return result;
}
}
