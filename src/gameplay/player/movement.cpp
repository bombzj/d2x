#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::stopWalking() {
    state_.player.route.clear();
    state_.player.attackTarget = {};
    state_.player.throwAttack = false;
    state_.player.leftHandAttack = false;
    state_.player.moving = false;
}
void Simulation::moveTo(Vec target) {
    auto &p = state_.player;
    if (p.dead || p.leapTime > 0 || p.spinTime > 0)
        return;
    p.attackTarget = {};
    p.throwAttack = false;
    p.leftHandAttack = false;
    p.route = grid_->path(p.pos, target);
    state_.message = p.route.empty() ? "That path is blocked" : "";
}
void Simulation::attackEnemy(EntityId target, bool thrown, bool leftHand) {
    auto &p = state_.player;
    auto *e = findEnemy(target);
    if (p.dead || !e || e->hp <= 0)
        return;
    if ((thrown || leftHand) && std::none_of(equipmentStats_.weapons.begin(),
            equipmentStats_.weapons.begin() + equipmentStats_.weaponCount,
            [=](const WeaponDamage &weapon) { return (!thrown || weapon.throwable) &&
                (!leftHand || weapon.leftHand); })) {
        state_.message = thrown ? "A throwing weapon is required." : "A left-hand weapon is required.";
        return;
    }
    p.attackTarget = target;
    p.throwAttack = thrown;
    p.leftHandAttack = leftHand;
    p.route = grid_->path(p.pos, e->pos);
}
void Simulation::updatePlayer(float dt, Vec keyboard) {
    auto &p = state_.player;
    const auto &rules = playerRules();
    p.mana = std::min(float(characterStats_.maxMana), p.mana + dt * characterStats_.manaRegen);
    Vec step;
    float remaining = 0;
    bool followingRoute = false;
    if (p.leapTime > 0) {
        const auto &skill = skillDefinition(Skill::Leap);
        p.leapTime = std::max(0.f, p.leapTime - dt);
        p.pos = p.leapStart + (p.leapEnd - p.leapStart) * (1 - p.leapTime / skill.duration);
        if (p.leapTime <= 0) {
            damage(p.pos, skill.radius, skill.damage, p.id);
            state_.area.effects.push_back({p.pos, Skill::Leap, 0, .5f});
        }
    }
    if (p.attackTarget) {
        auto *e = findEnemy(p.attackTarget);
        if (!e || e->hp <= 0)
            p.attackTarget = {};
        else {
            const WeaponDamage *weapon = &equipmentStats_.weapons[0];
            bool foundSelected = false;
            if (p.throwAttack || p.leftHandAttack)
                for (int index = 0; index < equipmentStats_.weaponCount; ++index)
                    if ((!p.throwAttack || equipmentStats_.weapons[index].throwable) &&
                        (!p.leftHandAttack || equipmentStats_.weapons[index].leftHand)) {
                        weapon = &equipmentStats_.weapons[index];
                        foundSelected = true;
                        break;
                    }
            if ((p.throwAttack || p.leftHandAttack) && !foundSelected) {
                p.attackTarget = {};
                p.throwAttack = false;
                p.leftHandAttack = false;
                state_.message = "The selected weapon is no longer equipped.";
            } else {
                const bool projectile = p.throwAttack || weapon->ranged;
                const float range = projectile ? weapon->missileSpeed * weapon->missileLifetime : rules.meleeRange;
                if ((e->pos - p.pos).length() < range && grid_->segment(p.pos, e->pos)) {
                    p.route.clear();
                    if (p.castTime <= 0 && p.meleeTime <= 0 && p.leapTime <= 0 && p.spinTime <= 0) {
                        p.look = (e->pos - p.pos).unit();
                        if (!projectile || firePhysicalProjectile(*e, *weapon, p.throwAttack)) {
                            p.lastMeleeDuration = rules.meleeDuration * (p.chill > 0 ? 2.f : 1.f);
                            p.meleeTime = p.lastMeleeDuration;
                            emit(MeleeAttack{p.id, e->id});
                            if (!projectile) meleeDamage(*e, p.leftHandAttack);
                        }
                    }
                } else if (p.route.empty())
                    p.route = grid_->path(p.pos, e->pos);
            }
        }
    }
    if (keyboard.length() > .1f && p.spinTime <= 0 && p.leapTime <= 0 && p.castTime <= 0) {
        p.route.clear();
        p.attackTarget = {};
        p.throwAttack = false;
        p.leftHandAttack = false;
        step = keyboard.unit();
    } else if (!p.route.empty() && p.castTime <= 0 && p.leapTime <= 0 && p.meleeTime <= 0) {
        while (!p.route.empty() && (p.route.front() - p.pos).length() < .01f)
            p.route.pop_front();
        if (!p.route.empty()) {
            auto delta = p.route.front() - p.pos;
            remaining = delta.length();
            followingRoute = true;
            step = delta.unit();
        }
    }
    float speed = p.spinTime > 0                              ? rules.spinSpeed
                  : p.running && (safeZone_ || p.stamina > 0) ? rules.runSpeed
                                               : rules.walkSpeed;
    if (p.chill > 0) speed *= .5f;
    if (p.webSlowRemaining > 0)
        speed *= 1.f + float(p.webSlowPercent) / 100.f;
    if (step.length() > .1f) {
        float distance = followingRoute ? std::min(dt * speed, remaining) : dt * speed;
        Vec next = p.pos + step * distance;
        if (grid_->segment(p.pos, next)) {
            p.pos = next;
            if (followingRoute && distance >= remaining)
                p.route.pop_front();
            p.moving = true;
            if (p.spinTime <= 0)
                p.look = step;
        } else
            p.route.clear();
    }
    p.stamina =
        std::clamp(p.stamina + dt * (!safeZone_ && p.moving && p.running && p.staminaBoost <= 0 ? -rules.staminaDrain
                                                                                  : rules.staminaRegen),
                   0.f, float(characterStats_.maxStamina));
    if (p.spinTime > 0) {
        const auto &skill = skillDefinition(Skill::Whirlwind);
        damage(p.pos, skill.radius, dt * skill.damage, p.id);
    }
}
} // namespace d2x
