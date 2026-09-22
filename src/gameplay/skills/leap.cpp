#include "execution.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void SkillSystem::leap(Simulation &simulation, Vec target, const SkillDefinition &skill) {
    auto &player = simulation.state_.player;
    player.leapTime = skill.duration;
    player.leapStart = player.pos;
    player.leapEnd = target;
}
} // namespace d2x