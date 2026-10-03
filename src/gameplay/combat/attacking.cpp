#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>

namespace d2x {
const WeaponDamage *Simulation::attackWeapon(bool thrown, bool leftHand) const {
    for (int index = 0; index < state_.player.equipment.weaponCount; ++index) {
        const auto &weapon = state_.player.equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if (thrown && !weapon.throwable) return nullptr;
        if (!thrown && weapon.potion) return nullptr; // SrvDo001 rejects missile potions.
        return &weapon;
    }
    return nullptr;
}
bool Simulation::meleeReach(EntityId defender, const WeaponDamage &weapon) const {
    auto target = const_cast<Simulation *>(this)->combatUnit(defender);
    return target.alive() && canAttack(state_.player.id, defender) && target.stats.collisionSize > 0 &&
        meleeDistance(state_.player.pos, 2, *target.position, target.stats.collisionSize) <= weapon.rangeAdder + 1 &&
        grid_->segment(state_.player.pos, *target.position);
}
void Simulation::requestAttack(const Attack &attack) {
    auto &p = state_.player;
    if (safeZone_ || p.dead || p.blockAnimation || p.charge || p.weaponAttack || p.castTime > 0 || p.hitTime > 0) return;
    auto enemy = combatUnit(attack.target);
    if ((!enemy.alive() || !canAttack(p.id, enemy.id)) && !attack.position) return;
    if (!attackWeapon(attack.thrown, attack.leftHand)) {
        state_.message = attack.thrown ? "A usable throwing weapon is required." :
                                        "No usable weapon for this attack.";
        return;
    }
    skills().stopChannel(skillCaster(p.id));
    p.approachSkill.reset();
    p.attackTarget = enemy.alive() && canAttack(p.id, enemy.id) ? enemy.id : EntityId{};
    p.attackPosition = attack.position;
    p.attackStationary = attack.stationary;
    p.throwAttack = attack.thrown;
    p.leftHandAttack = attack.leftHand;
    p.route.clear();
    if (p.attackTarget && !p.attackStationary) p.route = grid_->path(p.pos, *enemy.position, false, playerMovement);
}
bool Simulation::beginWeaponAttack(Vec aim, EntityId target, const WeaponDamage &weapon,
                                   bool thrown, bool leftHand, const WeaponSkillSpec *skill) {
    auto &p = state_.player;
    if (p.blockAnimation || p.charge || p.weaponAttack || p.castTime > 0 || p.hitTime > 0 || p.dead) return false;
    const auto timing = attackTiming_ ? attackTiming_(weapon, thrown, leftHand, skill ? std::string_view(skill->mode) : std::string_view{}) : std::nullopt;
    if (!timing) {
        state_.message = "Original attack animation is unavailable for this equipment.";
        return false;
    }
    if ((thrown || weapon.ranged) && !(skill && skill->smite) &&
        (!weapon.projectile || !canSpendProjectile_ || !canSpendProjectile_(weapon.item, thrown))) {
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    if ((aim - p.pos).length() < .001f) aim = p.pos + p.look;
    p.look = (aim - p.pos).unit();
    p.weaponAttack = WeaponAttackState{weapon.item, target, aim, *timing, thrown};
    p.weaponAttack->weaponClass = state_.player.equipment.animationClass;
    p.weaponAttack->appearanceDefinitions = state_.player.equipment.appearanceDefinitions;
    p.meleeTime = float(timing->durationTicks()) / 25.f;
    p.route.clear();
    emit(WeaponAttackStarted{p.id, target, thrown || weapon.ranged});
    return true;
}
} // namespace d2x
