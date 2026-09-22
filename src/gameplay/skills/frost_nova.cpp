#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::frostNova(Simulation &simulation, Vec, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    simulation.state_.area.effects.push_back({player.pos, skill.id, 0, skill.duration});
    simulation.damage(player.pos, skill.radius, skill.damage, player.id, skill.chill);
}
} // namespace d2x