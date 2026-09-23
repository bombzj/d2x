#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::stopWalking() {
    state_.player.route.clear();
    state_.player.attackTarget = {};
    state_.player.moving = false;
}
void Simulation::moveTo(Vec target) {
    auto &p = state_.player;
    if (p.dead || p.leapTime > 0 || p.spinTime > 0)
        return;
    p.attackTarget = {};
    p.route = grid_->path(p.pos, target);
    state_.message = p.route.empty() ? "That path is blocked" : "";
}
void Simulation::attackEnemy(EntityId target) {
    auto &p = state_.player;
    auto *e = findEnemy(target);
    if (p.dead || !e || e->hp <= 0)
        return;
    if (equipmentStats_.weapons[0].ranged) {
        p.attackTarget = {};
        p.route.clear();
        state_.message = "Ranged weapon attacks are not implemented yet.";
        return;
    }
    p.attackTarget = target;
    p.route = grid_->path(p.pos, e->pos);
}
void Simulation::updatePlayer(float dt, Vec keyboard) {
    auto &p = state_.player;
    const auto &rules = playerRules();
    p.mana = std::min(rules.maxMana, p.mana + dt * rules.manaRegen);
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
        if (!e || e->hp <= 0 || equipmentStats_.weapons[0].ranged)
            p.attackTarget = {};
        else if ((e->pos - p.pos).length() < rules.meleeRange && grid_->segment(p.pos, e->pos)) {
            p.route.clear();
            if (p.castTime <= 0 && p.meleeTime <= 0 && p.leapTime <= 0 && p.spinTime <= 0) {
                p.look = (e->pos - p.pos).unit();
                p.meleeTime = rules.meleeDuration;
                emit(MeleeAttack{p.id, e->id});
                meleeDamage(*e);
            }
        } else if (p.route.empty())
            p.route = grid_->path(p.pos, e->pos);
    }
    if (keyboard.length() > .1f && p.spinTime <= 0 && p.leapTime <= 0 && p.castTime <= 0) {
        p.route.clear();
        p.attackTarget = {};
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
                   0.f, rules.maxStamina);
    if (p.spinTime > 0) {
        const auto &skill = skillDefinition(Skill::Whirlwind);
        damage(p.pos, skill.radius, dt * skill.damage, p.id);
    }
}
} // namespace d2x
