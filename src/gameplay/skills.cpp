#include "simulation.hpp"

namespace d2x {
bool Simulation::cast(Skill id, Vec target) {
    auto &p = state_.player;
    auto &area = state_.area;
    auto index = size_t(id);
    if (index >= skillCount || p.dead || p.castTime > 0 || p.spinTime > 0 || p.leapTime > 0 ||
        p.cooldown[index] > 0)
        return false;
    const auto &skill = skillDefinition(id);
    if (p.mana < skill.manaCost) {
        state_.message = "Not enough mana";
        return false;
    }
    if ((id == Skill::Teleport || id == Skill::Leap) &&
        (!grid_->walkable(target) || (target - p.pos).length() > skill.range)) {
        state_.message = id == Skill::Teleport ? "Teleport needs clear ground within range"
                                               : "Leap needs clear ground within range";
        return false;
    }
    p.mana -= skill.manaCost;
    p.cooldown[index] = skill.cooldown;
    p.castTime = skill.castDuration;
    auto aim = (target - p.pos).unit();
    if (aim.length() > 0)
        p.look = aim;
    p.route.clear();
    p.attackTarget = {};
    p.lastSkill = id;
    state_.message.clear();
    emit(SkillCast{p.id, id, p.pos});
    switch (id) {
    case Skill::Fireball:
        area.missiles.push_back({ids_.allocate(), p.id, p.pos + p.look * .7f, p.look * skill.projectileSpeed,
                                 skill.duration, id});
        break;
    case Skill::FrostNova:
        area.effects.push_back({p.pos, id, 0, skill.duration});
        damage(p.pos, skill.radius, skill.damage, p.id, skill.chill);
        break;
    case Skill::Whirlwind:
        p.spinTime = skill.duration;
        p.route = grid_->path(p.pos, grid_->nearest(p.pos + p.look * skill.range));
        area.effects.push_back({p.pos, id, 0, skill.duration});
        break;
    case Skill::Teleport:
        area.effects.push_back({p.pos, id, 0, skill.duration});
        p.pos = p.previous = target;
        area.effects.push_back({p.pos, id, 0, skill.duration});
        break;
    case Skill::Leap:
        p.leapTime = skill.duration;
        p.leapStart = p.pos;
        p.leapEnd = target;
        break;
    case Skill::WarCry:
        area.effects.push_back({p.pos, id, 0, skill.duration});
        damage(p.pos, skill.radius, skill.damage, p.id);
        for (auto &e : area.enemies)
            if (e.hp > 0 && (e.pos - p.pos).length() < skill.radius)
                e.stun = skill.stun;
        break;
    case Skill::Count:
        return false;
    }
    return true;
}
} // namespace d2x
