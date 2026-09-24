#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void Simulation::activateSpiderWeb(Enemy &arach) {
    const auto web = monsterWeb_ ? monsterWeb_(arach) : std::nullopt;
    if (!web || arach.hp <= 0) return;
    arach.webAuraRemaining = web->auraDuration;
    arach.webTrailDistance = 0;
}
void Simulation::leaveSpiderWeb(Enemy &arach, float moved) {
    const auto web = monsterWeb_ ? monsterWeb_(arach) : std::nullopt;
    if (!web || arach.hp <= 0 || arach.webAuraRemaining <= 0 ||
        state_.area.missiles.size() >= 65536) return;
    arach.webTrailDistance += moved;
    if (arach.webTrailDistance < 1.f) return;
    arach.webTrailDistance -= 1.f;
    Missile missile;
    missile.id = ids_.allocate();
    missile.owner = arach.id;
    missile.pos = arach.pos;
    missile.remaining = web->lifetime;
    missile.missileId = web->missileId;
    missile.radius = web->radius;
    missile.hostile = true;
    missile.hostileMode = 7;
    missile.slowDuration = web->slowDuration;
    state_.area.missiles.push_back(missile);
}
} // namespace d2x
