#include "scaling.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <initializer_list>

namespace d2x {
int damageClassPercentage(DamageUnitClass source, DamageUnitClass target,
                          int hirelingBossPercentage, bool sameUnit) {
    if (sameUnit) return 100;
    if (target.player) return source.player || source.hireling || source.revived ? 17 : 100;
    if (source.hireling && target.hireling) return 25;
    if (source.hireling && target.boss) return hirelingBossPercentage;
    if (source.primeEvil && target.revived) return target.hireling ? 200 : 400;
    return 100;
}
void scaleHitDamage(MonsterHit &hit, int percentage) {
    if (percentage == 100) return;
    for (auto &channel : hit.channels)
        if (channel > 0) channel = nativePercentage(nativeChecked(channel), percentage, 100);
    for (auto *drain : {&hit.lifeDrain, &hit.mana, &hit.stamina})
        if (*drain > 0) *drain = nativePercentage(nativeChecked(*drain), percentage, 100);
}
}
