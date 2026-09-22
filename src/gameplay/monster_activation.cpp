#include "simulation.hpp"
#include <algorithm>
#include <set>

namespace d2x {
void Simulation::activateMonsters() {
    auto &pending = state_.area.pendingSpawns;
    if (pending.empty())
        return;
    std::set<uint32_t> groups;
    for (const auto &spawn : pending)
        if (active(spawn.position))
            groups.insert(spawn.identity.group);
    if (groups.empty())
        return;
    // Spawn an original pack together, once. Deferred instructions have no entity
    // IDs, AI, animation or minimap markers. Both lists survive travel and saves.
    std::vector<MonsterSpawn> entering;
    for (const auto &spawn : pending)
        if (groups.contains(spawn.identity.group))
            entering.push_back(spawn);
    spawnEnemies(entering);
    std::erase_if(pending, [&](const auto &spawn) { return groups.contains(spawn.identity.group); });
}
} // namespace d2x
