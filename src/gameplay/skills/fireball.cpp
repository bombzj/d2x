#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::fireball(Simulation &simulation, Vec, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    simulation.state_.area.missiles.push_back(
        {simulation.ids_.allocate(), player.id, player.pos + player.look * .7f,
         player.look * skill.projectileSpeed, skill.duration, skill.id});
}
} // namespace d2x