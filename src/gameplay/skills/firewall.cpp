#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/missile.hpp"
#include <utility>

namespace d2x {
void SkillRuntime::launchFirewall(EntityId actor, Vec center, Vec heading, const FirewallSpec &definition, SkillBehavior behavior) {
    for (int part = 0; part < 3; ++part) {
        const bool maker = part < 2;
        Missile missile{world_.allocate(), actor, center,
            maker ? heading * (part == 0 ? definition.velocity : -definition.velocity) : Vec{},
            float(maker ? definition.makerFrames : definition.fireFrames) / 25.f,
            behavior, false, maker ? definition.makerId : definition.fireId};
        missile.firewall = Missile::FirewallState{definition, maker, 0};
        missile.combatRandom = world_.childSeed();
        world_.addMissile(std::move(missile));
    }
}
} // namespace d2x
