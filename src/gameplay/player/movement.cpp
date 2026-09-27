#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::stopWalking() {
    state_.player.route.clear();
    state_.player.attackTarget = {};
    state_.player.attackPosition.reset();
    state_.player.throwAttack = false;
    state_.player.leftHandAttack = false;
    state_.player.moving = false;
}
void Simulation::moveTo(Vec target) {
    stopChannel(state_.player);
    auto &p = state_.player;
    if (p.dead)
        return;
    p.attackTarget = {};
    p.attackPosition.reset();
    p.throwAttack = false;
    p.leftHandAttack = false;
    p.route = grid_->path(p.pos, target, true);
    state_.message = p.route.empty() ? "That path is blocked" : "";
}
void Simulation::updatePlayer(float dt, Vec keyboard) {
    auto &p = state_.player;
    p.mana = std::min(float(characterStats_.maxMana), p.mana + dt * characterStats_.manaRegen);
    if (characterStats_.combat.replenishLife)
        p.hp = std::clamp(p.hp + dt * characterStats_.combat.replenishLife * 25.f / 256.f,
                          1.f, float(characterStats_.maxLife));
    Vec step;
    float remaining = 0;
    bool followingRoute = false;
    if (p.attackTarget || p.attackPosition) {
        auto *enemy = findEnemy(p.attackTarget);
        const auto *weapon = attackWeapon(p.throwAttack, p.leftHandAttack);
        if (!weapon || (p.attackTarget && (!enemy || enemy->hp <= 0))) {
            p.attackTarget = {};
            p.attackPosition.reset();
            p.route.clear();
        } else {
            const Vec aim = enemy ? enemy->pos : *p.attackPosition;
            const bool projectile = p.throwAttack || weapon->ranged;
            const bool inRange = p.attackStationary || !enemy || (projectile ?
                (weapon->projectile && missileDistance(p.pos, aim) <
                    weapon->projectile->speed * weapon->projectile->lifetime) : meleeReach(*enemy, *weapon));
            if (inRange) {
                p.route.clear();
                if (p.castTime <= 0 && p.meleeTime <= 0 && p.hitTime <= 0) {
                    beginWeaponAttack(aim, p.attackTarget, *weapon, p.throwAttack, p.leftHandAttack);
                    p.attackTarget = {};
                    p.attackPosition.reset();
                }
            } else if (p.route.empty() || (p.route.back() - aim).length() > 1)
                p.route = grid_->path(p.pos, aim);
        }
    }
    if (keyboard.length() > .1f && p.castTime <= 0 &&
        p.meleeTime <= 0 && p.hitTime <= 0) {
        p.route.clear();
        p.attackTarget = {};
        p.attackPosition.reset();
        p.throwAttack = false;
        p.leftHandAttack = false;
        step = keyboard.unit();
    } else if (!p.route.empty() && p.castTime <= 0 && p.meleeTime <= 0 && p.hitTime <= 0) {
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
    float speed = running ? characterStats_.runSpeed : characterStats_.walkSpeed;
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
}
} // namespace d2x
