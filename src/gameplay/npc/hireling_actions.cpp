#include "gameplay/skills/behavior.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void GameSessionImpl::advanceHirelingAttack(const MonsterRecord &actor, const HirelingCombatStats &stats) {
    auto &merc = simulation_->state_.player.hireling;
    auto &attack = *merc.attack;
    const bool release = advanceWeaponAction(attack);
    merc.attackTimer = weaponActionRemaining(attack);
    if (release) {
        if (!region().definition.safe && actor.attack1Projectile) {
            const auto &projectile = *actor.attack1Projectile;
            if (auto target = simulation_->combatUnit(attack.target); target.alive()) attack.aim = *target.position;
            merc.look = (attack.aim - merc.pos).unit();
            const auto &weapon = stats.weapon;
            const int spread = std::max(0, weapon.projectileMaximum - weapon.projectileMinimum);
            const int raw = weapon.projectileMinimum + int(limitedRandom(merc.combatRandom, unsigned(spread)));
            Missile missile{ids_.allocate(), merc.id, merc.pos, merc.look * projectile.velocity,
                projectile.lifetime, SkillBehavior::None, true, projectile.id,
                float(int64_t(raw) * projectile.sourceDamage / 128) / 256.f};
            missile.attackElements = simulation_->rollAttackElements(weapon.item, &stats.combat, nullptr, &merc.combatRandom);
            missile.attackElements.ranged = true;
            missile.attackElements.hitClass = weapon.hitClass;
            missile.weaponAttack = true;
            missile.attackElements.attackerLevel = merc.level;
            missile.attackElements.manaLeech = 0;
            missile.attackerLevel = merc.level;
            missile.attackRating = weapon.attackRating;
            missile.baseAttackRating = weapon.baseAttackRating;
            missile.attackRatingPercent = weapon.attackRatingPercent;
            missile.targetModifiers = weapon.target;
            missile.physicalDamagePercent = weapon.projectileDamagePercent;
            missile.combatRandom = childRandom(simulation_->unitRandom_);
            simulation_->state_.area.missiles.push_back(std::move(missile));
            simulation_->emit(MissileReleased{projectile.id});
        }
    }
    if (attack.ticks >= attack.timing.durationTicks()) { merc.attack.reset(); merc.attackTimer = 0; }
}
} // namespace d2x
