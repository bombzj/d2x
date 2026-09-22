#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::teleport(Simulation &simulation, Vec target, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    auto &effects = simulation.state_.area.effects;
    effects.push_back({player.pos, skill.id, 0, skill.duration});
    player.pos = player.previous = target;
    effects.push_back({player.pos, skill.id, 0, skill.duration});
}
} // namespace d2x