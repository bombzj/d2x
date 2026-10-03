#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/skills/weapon_port.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
const WeaponDamage *SkillRuntime::attackWeapon(WeaponSkillCaster actor, bool thrown, bool leftHand) const {
    for (int index = 0; index < actor.equipment.weaponCount; ++index) {
        const auto &weapon = actor.equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if (thrown && !weapon.throwable) return nullptr;
        if (!thrown && weapon.potion) return nullptr;
        return &weapon;
    }
    return nullptr;
}
bool SkillRuntime::beginWeaponSkill(WeaponSkillCaster p, const SkillCastSpec &skill, Vec aim, EntityId target) {
    if (!skill.weapon || world_.safeZone() || p.casting.dead || p.casting.blockAnimation || p.weaponAttack || p.casting.hitTime > 0) return false;
    const auto &action = *skill.weapon;
    if (p.charge) return false;
    if (action.delayFrames > 0 && world_.frame() < p.casting.skillDelayUntil) {
        world_.message("This skill is still recovering.");
        return false;
    }
    const auto *weapon = attackWeapon(p, action.thrown, false);
    if (!weapon || (action.smite ? !p.equipment.shield :
        std::find(weapon->types.begin(), weapon->types.end(), action.requiredType) == weapon->types.end())) {
        world_.message("This skill requires the matching weapon type.");
        return false;
    }
    if (action.chargeVelocity > 0 && target && weapons_.meleeReach(p.casting.id, target, *weapon)) {
        weapons_.requestWeaponAttack(p.casting.id, target, aim);
        return true;
    }
    if (p.casting.castTime > 0 || p.meleeTime > 0) return false;
    if (action.chargeVelocity == 0 && !action.thrown && (!weapon->ranged || action.smite) && target &&
        combatUnit(target).alive() && canAttack(p.casting.id, target) && !weapons_.meleeReach(p.casting.id, target, *weapon)) {
        p.approachSkill = skill;
        p.attackTarget = target;
        p.attackStationary = false;
        p.attackPosition.reset();
        p.throwAttack = p.leftHandAttack = false;
        p.route = weapons_.approach(p.casting.id, unitPosition(target));
        return !p.route.empty();
    }
    if (p.casting.mana < skill.manaCost) { world_.message("Not enough mana"); return false; }
    if (action.chargeVelocity > 0) {
        if (!weapons_.movementSegment(p.casting.id, p.casting.pos, aim)) return false;
        if ((aim - p.casting.pos).length() < .1f) return false;
        p.casting.mana -= skill.manaCost;
        auto charged = skill;
        charged.manaCost = 0;
        charged.weapon->chargeVelocity = 0;
        p.charge = ChargeSkillState{charged, aim, target,
            skill.missileVelocity * float(action.chargeVelocity + std::max(50, int(skill.staticPercent))) / 100.f};
        p.route.clear(); p.attackTarget = {}; p.attackPosition.reset();
        p.approachSkill.reset();
        stopChannel(p.casting);
        emit(SkillCast{p.casting.id, skill.sourceId, p.casting.pos});
        return true;
    }
    stopChannel(p.casting);
    if (skill.effect == SkillBehavior::Zeal) {
        if (!target || !weapons_.meleeReach(p.casting.id, target, *weapon)) {
            target = {};
            for (auto candidate : combatUnits())
                if (weapons_.meleeReach(p.casting.id, candidate.id, *weapon) && (!target || candidate.id.value < target.value)) target = candidate.id;
        }
        if (!target) return false;
    }
    if (auto enemy = combatUnit(target); enemy.alive() && canAttack(p.casting.id, enemy.id)) aim = *enemy.position;
    if (!weapons_.beginWeaponAttack(p.casting.id, aim, target, *weapon, action.thrown, false, &action)) return false;
    p.approachSkill.reset();
    p.weaponAttack->skill = skill;
    if (skill.effect == SkillBehavior::Charge) {
        p.weaponAttack->chargeSequence = true;
        p.weaponAttack->timing.frames = 8;
        p.weaponAttack->timing.actionFrame = 4;
        p.weaponAttack->timing.startFrame = 0;
        p.meleeTime = float(p.weaponAttack->timing.durationTicks()) / 25.f;
    }
    p.weaponAttack->remainingAttacks = action.attacks;
    p.attackTarget = {};
    p.attackPosition.reset();
    p.throwAttack = p.leftHandAttack = false;
    if (!action.manaOnRelease) p.casting.mana -= skill.manaCost;
    world_.message({});
    emit(SkillCast{p.casting.id, skill.sourceId, p.casting.pos});
    return true;
}
void SkillRuntime::advanceWeaponAttack(WeaponSkillCaster p) {
    if (!p.weaponAttack) return;
    if (p.casting.dead || p.life <= 0 || p.casting.hitTime > 0) {
        p.weaponAttack.reset();
        p.meleeTime = 0;
        return;
    }
    auto &attack = *p.weaponAttack;
    if (attack.skill && attack.skill->weapon->smite && !p.equipment.shield) {
        p.weaponAttack.reset(); p.meleeTime = 0; return;
    }
    ++attack.ticks;
    if (!attack.released && attack.ticks >= attack.timing.actionTick()) {
        attack.released = true;
        // Equipment can change during the wind-up. Never substitute another hand or fists.
        auto weapon = std::find_if(p.equipment.weapons.begin(),
            p.equipment.weapons.begin() + p.equipment.weaponCount,
            [&](const WeaponDamage &candidate) { return candidate.item == attack.weapon; });
        if (weapon != p.equipment.weapons.begin() + p.equipment.weaponCount &&
            p.equipment.animationClass == attack.weaponClass) {
            const auto selected = *weapon; // Consuming the last missile can refresh this cache.
            auto enemy = combatUnit(attack.target);
            const Vec aim = enemy.alive() && canAttack(p.casting.id, enemy.id) ? *enemy.position : attack.aim;
            if ((attack.thrown || selected.ranged) && !(attack.skill && attack.skill->weapon->smite))
                weapons_.fireWeaponProjectile(p.casting.id, aim, selected, attack.thrown, attack.skill ? &*attack.skill : nullptr);
            else if (enemy.alive() && canAttack(p.casting.id, enemy.id) && weapons_.meleeReach(p.casting.id, enemy.id, selected)) {
                auto melee = selected;
                if (attack.skill && attack.skill->weapon) {
                    melee.damagePercent += attack.skill->weapon->damagePercent;
                    melee.attackRatingPercent += attack.skill->weapon->attackRating;
                }
                if (attack.skill && attack.skill->weapon->smite && p.equipment.shield) {
                    const auto &mods = combatUnit(p.casting.id).stats.attributes.combat;
                    const int minimum = (p.equipment.smiteMinimum + mods.smiteMinimum + mods.normalDamage) * 256;
                    const int maximum = (p.equipment.smiteMaximum + mods.smiteMaximum + mods.normalDamage) * 256;
                    const int base = minimum + int(limitedRandom(p.casting.combatRandom, unsigned(std::max(0, maximum - minimum))));
                    const float damage = float(int64_t(base) * std::max(0, 100 + combatUnit(p.casting.id).stats.attributes.strength +
                        mods.damagePercent + attack.skill->weapon->damagePercent) / 100) / 256.f;
                    const auto triggers = weapons_.rollWeaponElements(p.casting.id, selected.item);
                    AttackElements shieldHit;
                    shieldHit.smite = true;
                    shieldHit.knockback = true;
                    shieldHit.crushing = triggers.crushing;
                    shieldHit.openWounds = triggers.openWounds;
                    shieldHit.attackerLevel = p.level;
                    shieldHit.hitClass = 101;
                    weapons_.weaponHit(p.casting.id, enemy.id, damage, shieldHit);
                    if (!p.weaponAttack) return;
                    if (enemy.alive() && enemy.monster && !enemy.stats.boss && weapons_.canStun(enemy.id)) {
                        const bool elite = enemy.stats.rank != MonsterRank::Normal;
                        if (!elite || limitedRandom(p.casting.combatRandom, 100) >= 90)
                            weapons_.stun(enemy.id, attack.skill->weapon->stunFrames);
                    }
                    if (weapons_.hasEquipmentWear()) weapons_.wearWeapon(selected.item);
                } else weapons_.weaponMelee(p.casting.id, enemy.id, melee, attack.skill ? &*attack.skill : nullptr);
                if (!p.weaponAttack) return;
                if (attack.skill && attack.skill->weapon->conversionFrames > 0 && enemy.alive() && enemy.monster &&
                    !enemy.stats.boss && enemy.stats.rank == MonsterRank::Normal &&
                    limitedRandom(p.casting.combatRandom, 100) < unsigned(attack.skill->weapon->conversionChance)) {
                    weapons_.convert(p.casting.id, enemy, *attack.skill, p.level);
                }
            }
            if (!p.weaponAttack) return;
            if (attack.skill && attack.skill->effect == SkillBehavior::Zeal && --attack.remainingAttacks > 0 && p.life > 0) {
                EntityId successor, fallback;
                for (auto candidate : combatUnits()) {
                    if (!weapons_.meleeReach(p.casting.id, candidate.id, selected)) continue;
                    if (!fallback || candidate.id.value < fallback.value) fallback = candidate.id;
                    if (candidate.id.value > attack.target.value && (!successor || candidate.id.value < successor.value))
                        successor = candidate.id;
                }
                const auto next = successor ? successor : fallback;
                if (next) {
                    attack.target = next;
                    p.casting.look = (unitPosition(next) - p.casting.pos).unit();
                    attack.ticks = attack.ticks * (100 - attack.skill->weapon->rollbackPercent) / 100;
                    attack.released = false;
                }
            }
        }
    }
    p.meleeTime = float(std::max(0, attack.timing.durationTicks() - attack.ticks)) / 25.f;
    if (p.meleeTime <= 0) p.weaponAttack.reset();
}
} // namespace d2x
