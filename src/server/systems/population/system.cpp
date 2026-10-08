#include "system.hpp"
#include "server/area_store.hpp"
namespace d2x::server::population {
DomainResult<EntityId> System::admit(const Spawn &spawn) { return ports_.monsters.admit(spawn); }
StepStatus System::step(TickContext, FrameFacts &) {
    bool blocked = false;
    for (const auto &[region, area] : ports_.areas.all()) {
        auto &population = state_.areas[region];
        if (population.generation == area.generation) continue;
        bool complete = true;
        for (const auto &spawn : area.definition.population) {
            if (population.admittedSpawnKeys.contains(spawn.identity.spawnKey)) continue;
            auto result = admit({spawn.identity, spawn.implementation, region, spawn.position, true, spawn.rule});
            if (!result) { complete = false; blocked = true; break; }
            population.admittedSpawnKeys.insert(spawn.identity.spawnKey);
        }
        if (complete) population.generation = area.generation;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
