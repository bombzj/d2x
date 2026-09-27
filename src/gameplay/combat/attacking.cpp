#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
const WeaponDamage *Simulation::attackWeapon(bool thrown, bool leftHand) const {
    for (int index = 0; index < equipmentStats_.weaponCount; ++index) {
        const auto &weapon = equipmentStats_.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if (thrown && !weapon.throwable) return nullptr;
        if (!thrown && weapon.potion) return nullptr; // SrvDo001 rejects missile potions.
        return &weapon;
    }
    return nullptr;
}
bool Simulation::meleeReach(const Enemy &enemy, const WeaponDamage &weapon) const {
    const int size = monsterSize_ ? monsterSize_(enemy) : 0;
    return size > 0 && meleeDistance(state_.player.pos, 2, enemy.pos, size) <= weapon.rangeAdder + 1 &&
           grid_->segment(state_.player.pos, enemy.pos);
}
void Simulation::requestAttack(const Attack &attack) {
    auto &p = state_.player;
    if (safeZone_ || p.dead || p.weaponAttack || p.castTime > 0 || p.hitTime > 0) return;
    auto *enemy = findEnemy(attack.target);
    if ((!enemy || enemy->hp <= 0) && !attack.position) return;
    if (!attackWeapon(attack.thrown, attack.leftHand)) {
        state_.message = attack.thrown ? "A usable throwing weapon is required." :
                                        "No usable weapon for this attack.";
        return;
    }
    stopChannel(p);
    p.attackTarget = enemy && enemy->hp > 0 ? enemy->id : EntityId{};
    p.attackPosition = attack.position;
    p.attackStationary = attack.stationary;
    p.throwAttack = attack.thrown;
    p.leftHandAttack = attack.leftHand;
    p.route.clear();
    if (p.attackTarget && !p.attackStationary) p.route = grid_->path(p.pos, enemy->pos);
}
bool Simulation::beginWeaponAttack(Vec aim, EntityId target, const WeaponDamage &weapon,
                                   bool thrown, bool leftHand) {
    auto &p = state_.player;
    if (p.weaponAttack || p.castTime > 0 || p.hitTime > 0 || p.dead) return false;
    const auto timing = attackTiming_ ? attackTiming_(weapon, thrown, leftHand) : std::nullopt;
    if (!timing) {
        state_.message = "Original attack animation is unavailable for this equipment.";
        return false;
    }
    if ((thrown || weapon.ranged) &&
        (!weapon.projectile || !canSpendProjectile_ || !canSpendProjectile_(weapon.item, thrown))) {
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    if ((aim - p.pos).length() < .001f) aim = p.pos + p.look;
    p.look = (aim - p.pos).unit();
    p.weaponAttack = WeaponAttackState{weapon.item, target, aim, *timing, thrown};
    p.weaponAttack->weaponClass = equipmentStats_.animationClass;
    p.weaponAttack->appearanceDefinitions = equipmentStats_.appearanceDefinitions;
    p.meleeTime = float(timing->durationTicks()) / 25.f;
    p.route.clear();
    emit(WeaponAttackStarted{p.id, target, thrown || weapon.ranged});
    return true;
}
bool Simulation::beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target) {
    auto &p = state_.player;
    if (!skill.weapon || safeZone_ || p.dead || p.weaponAttack || p.hitTime > 0) return false;
    const auto &action = *skill.weapon;
    if (action.delayFrames > 0 && state_.frame < p.skillDelayUntil) {
        state_.message = "This skill is still recovering.";
        return false;
    }
    const auto *weapon = attackWeapon(action.thrown, false);
    if (!weapon || std::find(weapon->types.begin(), weapon->types.end(), action.requiredType) == weapon->types.end()) {
        state_.message = "This skill requires the matching weapon type.";
        return false;
    }
    if (p.mana < skill.manaCost) { state_.message = "Not enough mana"; return false; }
    stopChannel(p);
    if (auto *enemy = findEnemy(target); enemy && enemy->hp > 0) aim = enemy->pos;
    if (!beginWeaponAttack(aim, target, *weapon, action.thrown, false)) return false;
    p.weaponAttack->skill = skill;
    p.attackTarget = {};
    p.attackPosition.reset();
    p.throwAttack = p.leftHandAttack = false;
    if (!action.manaOnRelease) p.mana -= skill.manaCost;
    state_.message.clear();
    emit(SkillCast{p.id, skill.sourceId, p.pos});
    return true;
}
void Simulation::advanceWeaponAttack() {
    auto &p = state_.player;
    if (!p.weaponAttack) return;
    if (p.dead || p.hp <= 0 || p.hitTime > 0) {
        p.weaponAttack.reset();
        p.meleeTime = 0;
        return;
    }
    auto &attack = *p.weaponAttack;
    ++attack.ticks;
    if (!attack.released && attack.ticks >= attack.timing.actionTick()) {
        attack.released = true;
        // Equipment can change during the wind-up. Never substitute another hand or fists.
        auto weapon = std::find_if(equipmentStats_.weapons.begin(),
            equipmentStats_.weapons.begin() + equipmentStats_.weaponCount,
            [&](const WeaponDamage &candidate) { return candidate.item == attack.weapon; });
        if (weapon != equipmentStats_.weapons.begin() + equipmentStats_.weaponCount &&
            equipmentStats_.animationClass == attack.weaponClass) {
            const auto selected = *weapon; // Consuming the last missile can refresh this cache.
            auto *enemy = findEnemy(attack.target);
            const Vec aim = enemy && enemy->hp > 0 ? enemy->pos : attack.aim;
            if (attack.thrown || selected.ranged)
                firePhysicalProjectile(aim, selected, attack.thrown, attack.skill ? &*attack.skill : nullptr);
            else if (enemy && enemy->hp > 0 && meleeReach(*enemy, selected))
                meleeDamage(*enemy, selected);
        }
    }
    p.meleeTime = float(std::max(0, attack.timing.durationTicks() - attack.ticks)) / 25.f;
    if (p.meleeTime <= 0) p.weaponAttack.reset();
}
} // namespace d2x
