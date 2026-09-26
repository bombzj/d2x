#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/execution.hpp"

namespace d2x {
bool Simulation::cast(Skill id, Vec target) {
    return SkillSystem::cast(*this, id, target);
}
bool SkillSystem::clearGround(const Simulation &simulation, Vec target, const SkillDefinition &skill) {
    return simulation.grid_->walkable(target) &&
            !((target - simulation.state_.player.pos).length() > skill.range);
}
bool SkillSystem::cast(Simulation &simulation, Skill id, Vec target) {
    struct Implementation {
        Effect effect;
        Validator validate;
        const char *failure;
    };
    static constexpr std::array<Implementation, skillCount> implementations{{
        {nullptr, nullptr, nullptr}, // Sorceress spells use the MPQ-derived original cast path.
        {nullptr, nullptr, nullptr},
        {whirlwind, nullptr, nullptr},
        {nullptr, nullptr, nullptr},
        {leap, clearGround, "Leap needs clear ground within range"},
        {warCry, nullptr, nullptr},
    }};
    auto &state = simulation.state_;
    auto &p = state.player;
    auto index = size_t(id);
    if (index >= skillCount || p.dead || p.castTime > 0 || p.spinTime > 0 || p.leapTime > 0 ||
        p.cooldown[index] > 0)
        return false;
    const auto &skill = skillDefinition(id);
    if (p.mana < skill.manaCost) {
        state.message = "Not enough mana";
        return false;
    }
    const auto &implementation = implementations[index];
    if (!implementation.effect)
        return false;
    if (implementation.validate && !implementation.validate(simulation, target, skill)) {
        state.message = implementation.failure;
        return false;
    }
    p.mana -= skill.manaCost;
    p.cooldown[index] = skill.cooldown;
    p.castTime = skill.castDuration;
    p.lastCastDuration = skill.castDuration;
    p.lastCastRate = 0;
    auto aim = (target - p.pos).unit();
    if (aim.length() > 0)
        p.look = aim;
    p.route.clear();
    p.attackTarget = {};
    p.lastSkill = id;
    state.message.clear();
    simulation.emit(SkillCast{p.id, id, p.pos});
    implementation.effect(simulation, target, skill);
    return true;
}
} // namespace d2x
