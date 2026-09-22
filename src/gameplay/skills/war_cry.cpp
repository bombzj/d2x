#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::warCry(Simulation &simulation, Vec, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    simulation.state_.area.effects.push_back({player.pos, skill.id, 0, skill.duration});
    simulation.damage(player.pos, skill.radius, skill.damage, player.id);
    for (auto &enemy : simulation.state_.area.enemies)
        if (enemy.hp > 0 && (enemy.pos - player.pos).length() < skill.radius)
            enemy.stun = skill.stun;
}
} // namespace d2x