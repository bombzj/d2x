#include "simulation.hpp"
#include "gameplay/units/periodic_damage.hpp"
#include <algorithm>

namespace d2x {
void Simulation::advanceUnitDamage(float dt) {
    const PeriodicDamageHandler applyPeriodicDamage = [this](const DamageRequest &request) { dealDamage(request); };
    for (auto unit : combatUnits()) {
        if (!active(*unit.position)) continue;
        if (unit.monster) {
            unit.records.monster->hitFlash = std::max(0.f, unit.records.monster->hitFlash - dt);
            if (!unit.alive()) unit.records.monster->deathAge += dt;
        }
        if (!unit.alive()) continue;
        advancePeriodicDamage(unit, dt, unit.player || safeZone_, applyPeriodicDamage);
    }
}
void Simulation::finishWorldStep(float dt) {
    const auto &p = state_.player;
    if (!p.actions.dead && state_.area.pendingSpawns.empty() && !state_.area.enemies.empty() &&
        state_.area.kills == int(state_.area.enemies.size()))
        state_.message = "Area cleared. Ctrl+F2: travel onward. Ctrl+R: repopulate the area.";
    for (auto &e : state_.area.effects)
        e.age += dt;
    std::erase_if(state_.area.effects, [](const Effect &e) { return e.age >= e.duration; });
}
} // namespace d2x
