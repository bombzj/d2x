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
    stopChannel(state_.player);
    auto &p = state_.player;
    if (p.dead || p.leapTime > 0 || p.spinTime > 0)
        return;
    p.attackTarget = {};
    p.throwAttack = false;
    p.leftHandAttack = false;
    p.route = grid_->path(p.pos, target, true);
    state_.message = p.route.empty() ? "That path is blocked" : "";
}
void Simulation::attackEnemy(EntityId target, bool thrown, bool leftHand) {
    if (safeZone_) return;
    stopChannel(state_.player);
    auto &p = state_.player;
    auto *e = findEnemy(target);
    if (p.dead || !e || e->hp <= 0 || p.meleeTime > 0)
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
    if (characterStats_.combat.replenishLife)
        p.hp = std::clamp(p.hp + dt * characterStats_.combat.replenishLife * 25.f / 256.f,
                          1.f, float(characterStats_.maxLife));
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
        if (!e || e->hp <= 0) {
            p.attackTarget = {};
            p.route.clear();
        } else {
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
                const bool clear = projectile ? missilePathClear(weapon->missileId, p.pos, e->pos)
                                              : grid_->segment(p.pos, e->pos);
                if ((e->pos - p.pos).length() < range && clear) {
                    p.route.clear();
                    if (p.castTime <= 0 && p.meleeTime <= 0 && p.leapTime <= 0 && p.spinTime <= 0) {
                        p.look = (e->pos - p.pos).unit();
                        if (!projectile || firePhysicalProjectile(*e, *weapon, p.throwAttack)) {
                            p.lastMeleeDuration = rules.meleeDuration * (p.chill > 0 ? 2.f : 1.f);
                            p.meleeTime = p.lastMeleeDuration;
                            emit(MeleeAttack{p.id, e->id});
                            if (!projectile) meleeDamage(*e, p.leftHandAttack);
                            p.attackTarget = {};
                        }
                    }
                } else if (p.route.empty() || (p.route.back() - e->pos).length() > 1)
                    p.route = grid_->path(p.pos, e->pos);
            }
        }
    }
    if (keyboard.length() > .1f && p.spinTime <= 0 && p.leapTime <= 0 && p.castTime <= 0 &&
        p.meleeTime <= 0) {
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
    const bool running = (p.running || forceRun_) && (safeZone_ || p.stamina > 0);
    p.runningNow = running;
    float speed = p.spinTime > 0 ? rules.spinSpeed
                                : running ? characterStats_.runSpeed : characterStats_.walkSpeed;
    if (p.chill > 0) speed *= .5f;
    if (p.webSlowRemaining > 0)
        speed *= std::max(0.f, 1.f + float(p.webSlowPercent) / 100.f);
    if (step.length() > .1f) {
        float distance = followingRoute ? std::min(dt * speed, remaining) : dt * speed;
        Vec next = p.pos + step * distance;
        if (!followingRoute && distance > .0001f && !grid_->segment(p.pos, next)) {
            float clear = 0, blocked = distance;
            for (int iteration = 0; iteration < 12; ++iteration) {
                const float middle = (clear + blocked) * .5f;
                if (grid_->segment(p.pos, p.pos + step * middle)) clear = middle;
                else blocked = middle;
            }
            distance = clear;
            next = p.pos + step * distance;
        }
        if (distance > 0.0001f && grid_->segment(p.pos, next)) {
            p.pos = next;
            if (followingRoute && distance >= remaining)
                p.route.pop_front();
            p.moving = true;
            if (p.spinTime <= 0)
                p.look = step;
        } else if (distance > 0.0001f)
            p.route.clear();
    }
    float staminaRate = 0;
    if (p.moving && running && !safeZone_ && p.staminaBoost <= 0)
        staminaRate = -characterStats_.staminaDrain;
    else if (!p.moving || !running || characterStats_.staminaRecoveryBonus >= 1000) {
        // D2Game regenerates 1/256 of maximum stamina per frame while idle,
        // half that while walking; movement at zero stamina must first stop.
        if (!p.moving || p.stamina >= 1.f || safeZone_) {
            const float factor = p.moving && !running ? .5f : 1.f;
            staminaRate = float(characterStats_.maxStamina) * 25.f / 256.f * factor *
                          std::max(0.f, 1.f + characterStats_.staminaRecoveryBonus / 100.f);
        }
    }
    p.stamina = std::clamp(p.stamina + dt * staminaRate, 0.f, float(characterStats_.maxStamina));
    if (p.spinTime > 0) {
        const auto &skill = skillDefinition(Skill::Whirlwind);
        damage(p.pos, skill.radius, dt * skill.damage, p.id);
    }
}
} // namespace d2x
