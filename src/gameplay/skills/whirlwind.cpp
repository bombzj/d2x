#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::whirlwind(Simulation &simulation, Vec, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    player.spinTime = skill.duration;
    player.route = simulation.grid_->path(
        player.pos, simulation.grid_->nearest(player.pos + player.look * skill.range));
    simulation.state_.area.effects.push_back({player.pos, skill.id, 0, skill.duration});
}
} // namespace d2x