#include "gameplay/simulation/simulation.hpp"
#include "damage_resolution.hpp"
#include <algorithm>
#include "core/random.hpp"

namespace d2x {
Vec Simulation::monsterTargetPosition(const Enemy &enemy) const {
    return enemy.targetHireling ? state_.player.hireling.pos : state_.player.pos;
}
float Simulation::hurtHireling(float amount, MonsterDamageType type, bool hitRecovery, bool alreadyMitigated) {
    auto &merc = state_.player.hireling;
    if (!merc.active() || !hirelingAttributes_) return 0;
    const auto stats = hirelingAttributes_();
    auto damage = mitigatePlayerDamage(amount, type, stats);
    if (alreadyMitigated) { damage.dealt = amount; damage.absorbed = 0; }
    merc.hp = std::min(float(stats.maxLife), merc.hp + damage.absorbed);
    const auto dealt = std::min(merc.hp, damage.dealt);
    merc.hp = std::max(0.f, merc.hp - damage.dealt);
    if (merc.hp <= 0) {
        merc.attack.reset(); merc.attackTimer = 0;
        merc.route.clear(); merc.moving = false; merc.hitTime = 0;
        merc.healing.clear(); merc.chill = merc.poisonRemaining = merc.poisonPerSecond = 0;
        merc.combatEffects.onDeath(EffectUnitKind::Monster);
        merc.corpseRegion = state_.area.region; merc.corpseVisible = true; merc.deathAge = 0;
    } else if (hitRecovery) recoverHireling(dealt);
    return dealt;
}
float Simulation::hirelingIncomingDamage(const Enemy &enemy, float damage) const {
    return damage * (monsterHitProperties_ && monsterHitProperties_(enemy).second ? 2.f : 1.f);
}
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
float Simulation::hurtPlayer(float amount, MonsterDamageType type) {
    auto &player = state_.player;
    if (player.dead || player.hp <= 0) return 0;
    const auto resolved = mitigatePlayerDamage(amount, type, characterStats_);
    player.hp = std::min(float(characterStats_.maxLife), player.hp + resolved.absorbed);
    const float dealt = std::min(player.hp, resolved.dealt);
    player.hp = std::max(0.f, player.hp - resolved.dealt);
    if (dealt > 0) player.hitTime = .16f;
    return dealt;
}
} // namespace d2x
