#include "gameplay/simulation/simulation.hpp"
#include "damage_resolution.hpp"
#include <algorithm>
#include "core/random.hpp"

namespace d2x {
void Simulation::recoverHireling(float damage, int hitClass) {
    auto &merc = state_.player.hireling;
    if (!merc.active() || damage < 1 || merc.baseHitDuration <= 0 || !hirelingAttributes_) return;
    const auto stats = hirelingAttributes_();
    const int divisor = hitClass == 2 || hitClass == 6 || hitClass == 10 || hitClass == 11 ? 8 :
                        hitClass == 5 ? 64 : hitClass == 4 || hitClass == 8 ? 32 : 16;
    const int dealt = int(damage * 256), maximum = stats.maxLife * 256;
    if (dealt < maximum / divisor ||
        (dealt < maximum / (divisor / 2) && !(rollRandom(merc.combatRandom) & 1)) ||
        (dealt < maximum / (divisor / 4) && !(rollRandom(merc.combatRandom) & 3))) return;
    const int fhr = std::max(0, stats.combat.fasterHitRecovery);
    merc.hitTime = merc.hitDuration = merc.baseHitDuration * 100.f / (50 + 120 * fhr / (120 + fhr));
    merc.attack.reset(); merc.attackTimer = 0;
    merc.route.clear(); merc.moving = false;
}
} // namespace d2x
