#include "simulation.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/caster.hpp"

namespace d2x {
void Simulation::tick(float dt, const PlayerFrameInput &input) {
    if (!grid_ || dt <= 0) return;
    const auto control = input.actor == state_.player.id ? input : PlayerFrameInput{};
    beginPlayerStep(dt, control);
    advanceUnitDamage(dt);
    advancePlayerStep(dt, control);
    updateMonsterEnchantments();
    skills().updateAuras();
    skills().advanceThunderStorm(skillCaster(state_.player.id));
    updateMonsters(dt);
    updateCompanions(dt);
    updateMissiles(dt);
    finishPlayerStep();
    finishWorldStep(dt);
}
} // namespace d2x
