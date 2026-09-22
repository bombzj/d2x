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
    p.attackTarget = target;
    p.route = grid_->path(p.pos, e->pos);
}
void Simulation::updatePlayer(float dt, Vec keyboard) {
    auto &p = state_.player;
    const auto &rules = playerRules();
    p.mana = std::min(rules.maxMana, p.mana + dt * rules.manaRegen);
    Vec step;
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
        else if ((e->pos - p.pos).length() < rules.meleeRange) {
            p.route.clear();
            if (p.castTime <= 0 && p.meleeTime <= 0 && p.leapTime <= 0 && p.spinTime <= 0) {
                p.look = (e->pos - p.pos).unit();
                p.meleeTime = rules.meleeDuration;
                emit(MeleeAttack{p.id, e->id});
                damage(e->pos, rules.meleeRadius, rules.meleeDamage, p.id);
            }
        } else if (p.route.empty())
            p.route = grid_->path(p.pos, e->pos);
    }
    if (keyboard.length() > .1f && p.spinTime <= 0 && p.leapTime <= 0 && p.castTime <= 0) {
        p.route.clear();
        p.attackTarget = {};
        step = keyboard.unit();
    } else if (!p.route.empty() && p.castTime <= 0 && p.leapTime <= 0 && p.meleeTime <= 0) {
        auto delta = p.route.front() - p.pos;
        if (delta.length() < .2f)
            p.route.pop_front();
        else
            step = delta.unit();
    }
    float speed = p.spinTime > 0               ? rules.spinSpeed
                  : p.running && p.stamina > 0 ? rules.runSpeed
                                               : rules.walkSpeed;
    if (step.length() > .1f) {
        Vec next = p.pos + step * (dt * speed);
        if (grid_->segment(p.pos, next)) {
            p.pos = next;
            p.moving = true;
            if (p.spinTime <= 0)
                p.look = step;
        } else
            p.route.clear();
    }
    p.stamina =
        std::clamp(p.stamina + dt * (p.moving && p.running && p.staminaBoost <= 0 ? -rules.staminaDrain
                                                                                  : rules.staminaRegen),
                   0.f, rules.maxStamina);
    if (p.spinTime > 0) {
        const auto &skill = skillDefinition(Skill::Whirlwind);
        damage(p.pos, skill.radius, dt * skill.damage, p.id);
    }
}
} // namespace d2x
