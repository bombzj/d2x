#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include <algorithm>

namespace d2x {
bool Simulation::firePhysicalProjectile(const Enemy &enemy, const WeaponDamage &weapon, bool thrown) {
    auto &player = state_.player;
    const auto selected = weapon; // Ammo consumption can rebuild the equipment cache.
    const auto priorRandom = player.combatRandom;
    auto elements = rollAttackElements(selected.item);
    elements.ranged = true;
    if (!spendProjectile_ || !spendProjectile_(selected.item, thrown)) {
        player.combatRandom = priorRandom;
        player.attackTarget = {};
        player.throwAttack = false;
        player.leftHandAttack = false;
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                          (player.combatRandom >> 32);
    const int minimum = thrown ? selected.throwMinimum : selected.minimum;
    const int maximum = thrown ? selected.throwMaximum : selected.maximum;
    const uint64_t span = uint64_t(maximum) - uint64_t(minimum) + 1;
    const float damage = float(int64_t(minimum) + int64_t(uint32_t(player.combatRandom) % span)) / 256.f;
    const Vec direction = (enemy.pos - player.pos).unit();
    state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos,
        direction * selected.missileSpeed, selected.missileLifetime, Skill::Fireball,
        true, selected.missileId, damage});
    state_.area.missiles.back().attackElements = elements;
    if (wearEquipment_ && !thrown)
        wearEquipment_(selected.item, false);
    return true;
}
} // namespace d2x
