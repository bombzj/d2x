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
    stopChannel(p);
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
bool Simulation::beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target) {
    auto &p = state_.player;
    if (!skill.weapon || safeZone_ || p.dead || p.blockAnimation || p.weaponAttack || p.hitTime > 0) return false;
    const auto &action = *skill.weapon;
    if (p.charge) return false;
    if (action.delayFrames > 0 && state_.frame < p.skillDelayUntil) {
        state_.message = "This skill is still recovering.";
        return false;
    }
    const auto *weapon = attackWeapon(action.thrown, false);
    if (!weapon || (action.smite ? !p.equipment.shield :
        std::find(weapon->types.begin(), weapon->types.end(), action.requiredType) == weapon->types.end())) {
        state_.message = "This skill requires the matching weapon type.";
        return false;
    }
    if (action.chargeVelocity > 0 && target && meleeReach(target, *weapon)) {
        requestAttack(Attack{target, false, false, aim, false});
        return true;
    }
    if (p.castTime > 0 || p.meleeTime > 0) return false;
    if (action.chargeVelocity == 0 && !action.thrown && (!weapon->ranged || action.smite) && target &&
        combatUnit(target).alive() && canAttack(p.id, target) && !meleeReach(target, *weapon)) {
        p.approachSkill = skill;
        p.attackTarget = target;
        p.attackStationary = false;
        p.attackPosition.reset();
        p.throwAttack = p.leftHandAttack = false;
        p.route = grid_->path(p.pos, unitPosition(target), false, playerMovement);
        return !p.route.empty();
    }
    if (p.mana < skill.manaCost) { state_.message = "Not enough mana"; return false; }
    if (action.chargeVelocity > 0) {
        if (!grid_->segment(p.pos, aim, {}, playerMovement)) return false;
        if ((aim - p.pos).length() < .1f) return false;
        p.mana -= skill.manaCost;
        auto charged = skill;
        charged.manaCost = 0;
        charged.weapon->chargeVelocity = 0;
        p.charge = PlayerState::ChargeState{charged, aim, target,
            skill.missileVelocity * float(action.chargeVelocity + std::max(50, int(skill.staticPercent))) / 100.f};
        p.route.clear(); p.attackTarget = {}; p.attackPosition.reset();
        p.approachSkill.reset();
        stopChannel(p);
        emit(SkillCast{p.id, skill.sourceId, p.pos});
        return true;
    }
    stopChannel(p);
    if (skill.effect == SkillBehavior::Zeal) {
        if (!target || !meleeReach(target, *weapon)) {
            target = {};
            for (auto candidate : combatUnits())
                if (meleeReach(candidate.id, *weapon) && (!target || candidate.id.value < target.value)) target = candidate.id;
        }
        if (!target) return false;
    }
    if (auto enemy = combatUnit(target); enemy.alive() && canAttack(p.id, enemy.id)) aim = *enemy.position;
    if (!beginWeaponAttack(aim, target, *weapon, action.thrown, false, &action)) return false;
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
    if (attack.skill && attack.skill->weapon->smite && !p.equipment.shield) {
        p.weaponAttack.reset(); p.meleeTime = 0; return;
    }
    ++attack.ticks;
    if (!attack.released && attack.ticks >= attack.timing.actionTick()) {
        attack.released = true;
        // Equipment can change during the wind-up. Never substitute another hand or fists.
        auto weapon = std::find_if(state_.player.equipment.weapons.begin(),
            state_.player.equipment.weapons.begin() + state_.player.equipment.weaponCount,
            [&](const WeaponDamage &candidate) { return candidate.item == attack.weapon; });
        if (weapon != state_.player.equipment.weapons.begin() + state_.player.equipment.weaponCount &&
            state_.player.equipment.animationClass == attack.weaponClass) {
            const auto selected = *weapon; // Consuming the last missile can refresh this cache.
            auto enemy = combatUnit(attack.target);
            const Vec aim = enemy.alive() && canAttack(p.id, enemy.id) ? *enemy.position : attack.aim;
            if ((attack.thrown || selected.ranged) && !(attack.skill && attack.skill->weapon->smite))
                firePhysicalProjectile(aim, selected, attack.thrown, attack.skill ? &*attack.skill : nullptr);
            else if (enemy.alive() && canAttack(p.id, enemy.id) && meleeReach(enemy.id, selected)) {
                auto melee = selected;
                if (attack.skill && attack.skill->weapon) {
                    melee.damagePercent += attack.skill->weapon->damagePercent;
                    melee.attackRatingPercent += attack.skill->weapon->attackRating;
                }
                if (attack.skill && attack.skill->weapon->smite && p.equipment.shield) {
                    const auto &mods = combatUnit(p.id).stats.attributes.combat;
                    const int minimum = (p.equipment.smiteMinimum + mods.smiteMinimum + mods.normalDamage) * 256;
                    const int maximum = (p.equipment.smiteMaximum + mods.smiteMaximum + mods.normalDamage) * 256;
                    const int base = minimum + int(limitedRandom(p.combatRandom, unsigned(std::max(0, maximum - minimum))));
                    DamageRequest hit{p.id, enemy.id, float(int64_t(base) * std::max(0, 100 + combatUnit(p.id).stats.attributes.strength +
                        mods.damagePercent + attack.skill->weapon->damagePercent) / 100) / 256.f, MonsterDamageType::Physical};
                    hit.hitClass = 101;
                    const auto triggers = rollAttackElements(selected.item);
                    AttackElements shieldHit;
                    shieldHit.smite = true;
                    shieldHit.knockback = true;
                    shieldHit.crushing = triggers.crushing;
                    shieldHit.openWounds = triggers.openWounds;
                    shieldHit.attackerLevel = p.level;
                    shieldHit.hitClass = 101;
                    resolveWeaponHit(enemy.id, hit.amount, p.id, shieldHit);
                    if (enemy.alive() && enemy.monster && !enemy.stats.boss && monsterWalkSpeed_ &&
                        monsterWalkSpeed_(*enemy.monster).value_or(0) > 0) {
                        const bool elite = enemy.stats.rank != MonsterRank::Normal;
                        if (!elite || limitedRandom(p.combatRandom, 100) >= 90)
                            enemy.monster->stun = std::max(enemy.monster->stun, float(attack.skill->weapon->stunFrames) / 25.f);
                    }
                    if (wearEquipment_) wearEquipment_(selected.item, false);
                } else meleeDamage(enemy.id, melee);
                if (attack.skill && attack.skill->weapon->conversionFrames > 0 && enemy.alive() && enemy.monster &&
                    !enemy.stats.boss && enemy.stats.rank == MonsterRank::Normal &&
                    limitedRandom(p.combatRandom, 100) < unsigned(attack.skill->weapon->conversionChance)) {
                    auto &monster = *enemy.monster;
                    monster.conversion = Enemy::ConversionState{monster.allegiance,
                        state_.frame + EffectFrame(attack.skill->weapon->conversionFrames), enemy.stats.level, p.level, monster.maxHp};
                    monster.conversion->state = attack.skill->weapon->conversionState.id;
                    std::vector<EffectHandle> curses;
                    for (const auto &effect : monster.combatEffects.entries())
                        if (effect.spec.state.curse) curses.push_back(effect.handle);
                    for (const auto handle : curses) monster.combatEffects.remove(handle);
                    CombatEffectSpec converted;
                    converted.state = attack.skill->weapon->conversionState;
                    converted.source = {CombatEffectSource::Skill, p.id, attack.skill->sourceId, attack.skill->rank};
                    converted.duration = EffectFrame(attack.skill->weapon->conversionFrames);
                    monster.combatEffects.apply(std::move(converted), state_.frame);
                    if (enemy.stats.level > p.level) {
                        monster.maxHp = std::max(1.f / 256.f, float(int(monster.maxHp) * p.level / enemy.stats.level));
                        monster.hp = std::clamp(float(int(monster.hp) * p.level / enemy.stats.level), 1.f / 256.f, monster.maxHp);
                    }
                    monster.allegiance.faction = p.allegiance.faction;
                    monster.allegiance.owner = p.id;
                    monster.combatTarget = {};
                    monsterStopApproach(monster);
                    monster.route.clear(); monster.attack = monster.attackDuration = 0; monster.attackImpact = -1;
                    monster.aiPursuing = monster.aiEscaping = monster.aiCircling = monster.aiRunning = false;
                    monster.aiCorpse = {};
                    monster.skill2Remaining = monster.skill2Duration = 0;
                    monster.rethink = 0;
                }
            }
            if (attack.skill && attack.skill->effect == SkillBehavior::Zeal && --attack.remainingAttacks > 0 && p.hp > 0) {
                EntityId successor, fallback;
                for (auto candidate : combatUnits()) {
                    if (!meleeReach(candidate.id, selected)) continue;
                    if (!fallback || candidate.id.value < fallback.value) fallback = candidate.id;
                    if (candidate.id.value > attack.target.value && (!successor || candidate.id.value < successor.value))
                        successor = candidate.id;
                }
                const auto next = successor ? successor : fallback;
                if (next) {
                    attack.target = next;
                    p.look = (unitPosition(next) - p.pos).unit();
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
