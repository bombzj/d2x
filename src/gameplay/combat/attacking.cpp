#include "gameplay/units/actions.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>

namespace d2x {
const WeaponDamage *Simulation::attackWeapon(bool thrown, bool leftHand) const {
    return selectAttackWeapon(state_.player.equipment, thrown, leftHand);
}
bool Simulation::meleeReach(EntityId defender, const WeaponDamage &weapon) const {
    auto target = const_cast<Simulation *>(this)->combatUnit(defender);
    return target.alive() && canAttack(state_.player.id, defender) && target.stats.collisionSize > 0 &&
        meleeDistance(state_.player.movement.pos, 2, *target.position, target.stats.collisionSize) <= weapon.rangeAdder + 1 &&
        grid_->segment(state_.player.movement.pos, *target.position);
}
void Simulation::requestAttack(const Attack &attack) {
    auto &p = state_.player;
    if (safeZone_ || p.actions.dead || p.actions.blockAnimation || p.actions.charge || p.actions.weaponAttack || p.actions.castTime > 0 || p.actions.hitTime > 0) return;
    auto enemy = combatUnit(attack.target);
    if ((!enemy.alive() || !canAttack(p.id, enemy.id)) && !attack.position) return;
    if (!attackWeapon(attack.thrown, attack.leftHand)) {
        state_.message = attack.thrown ? "A usable throwing weapon is required." :
                                        "No usable weapon for this attack.";
        return;
    }
    skills().stopChannel(skillCaster(p.id));
    p.actions.approachSkill.reset();
    p.actions.attackTarget = enemy.alive() && canAttack(p.id, enemy.id) ? enemy.id : EntityId{};
    p.actions.attackPosition = attack.position;
    p.actions.attackStationary = attack.stationary;
    p.actions.throwAttack = attack.thrown;
    p.actions.leftHandAttack = attack.leftHand;
    p.movement.route.clear();
    if (p.actions.attackTarget && !p.actions.attackStationary) p.movement.route = grid_->path(p.movement.pos, *enemy.position, false, playerMovement);
}
bool Simulation::beginWeaponAttack(Vec aim, EntityId target, const WeaponDamage &weapon,
                                   bool thrown, bool leftHand, const WeaponSkillSpec *skill) {
    auto &p = state_.player;
    if (p.actions.blockAnimation || p.actions.charge || p.actions.weaponAttack || p.actions.castTime > 0 || p.actions.hitTime > 0 || p.actions.dead) return false;
    const auto timing = attackTiming_ ? attackTiming_(weapon, thrown, leftHand, skill ? std::string_view(skill->mode) : std::string_view{}) : std::nullopt;
    if (!timing) {
        state_.message = "Original attack animation is unavailable for this equipment.";
        return false;
    }
    if ((thrown || weapon.ranged) && !(skill && (skill->smite || skill->noAmmo)) &&
        (!weapon.projectile || !canSpendProjectile_ || !canSpendProjectile_(weapon.item, thrown))) {
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    if ((aim - p.movement.pos).length() < .001f) aim = p.movement.pos + p.movement.look;
    p.movement.look = (aim - p.movement.pos).unit();
    const auto selected = weapon;
    if (skill && skill->bow && skill->bow->strafe &&
        (!spendProjectile_ || !spendProjectile_(selected.item, thrown))) return false;
    WeaponAttackState attack;
    attack.weapon = selected.item; attack.target = target; attack.aim = aim; attack.timing = *timing; attack.thrown = thrown;
    p.actions.weaponAttack = std::move(attack);
    p.actions.weaponAttack->weaponClass = state_.player.equipment.animationClass;
    p.actions.weaponAttack->appearanceDefinitions = state_.player.equipment.appearanceDefinitions;
    p.actions.meleeTime = float(timing->durationTicks()) / 25.f;
    p.movement.route.clear();
    emit(WeaponAttackStarted{p.id, target, thrown || weapon.ranged});
    return true;
}
} // namespace d2x
