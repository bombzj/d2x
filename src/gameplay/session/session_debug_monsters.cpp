#include "gameplay/monsters/implementation.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
std::optional<uint32_t> nextDebugGroup(const AreaState &area) {
    uint32_t largest = 0;
    for (const auto &enemy : area.enemies)
        largest = std::max(largest, enemy.identity.group);
    for (const auto &spawn : area.pendingSpawns)
        largest = std::max(largest, spawn.identity.group);
    if (largest == std::numeric_limits<uint32_t>::max()) return std::nullopt;
    return largest + 1;
}
} // namespace

std::string GameSessionImpl::debugSpawnError(std::string_view monster, Vec position) const {
    if (state().player.actions.dead || simulation_->safeZone_)
        return "Monster spawn requires a living player outside town";
    const auto *record = monsterContent_.find(monster);
    if (!record || !record->hostile())
        return "Monster ID must name a hostile MonStats record in the mounted MPQ";
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        position.x < 0 || position.y < 0 || position.x >= map().grid.width ||
        position.y >= map().grid.height || !map().grid.walkable(position))
        return "Monster spawn position must be a walkable cell in the current region";
    if ((position - state().player.movement.pos).length() < .8f ||
        std::any_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const Enemy &enemy) {
            return enemy.hp > 0 && (position - enemy.pos).length() < .8f;
        }))
        return "Monster spawn position overlaps a living actor";
    if (state().area.enemies.size() >= 65536 || !nextDebugGroup(state().area))
        return "Monster spawn limit reached";
    return {};
}

void GameSessionImpl::spawnDebugMonster(const DebugSpawnMonster &command) {
    if (!debugSpawnError(command.monster, command.position).empty()) return;
    const auto *record = monsterContent_.find(command.monster);
    const uint32_t group = *nextDebugGroup(state().area);
    MonsterSpawn spawn{{record->id, {}, "debug." + std::to_string(group),
                        record->boss ? MonsterRank::Boss : MonsterRank::Normal,
                        SpawnOrigin::Debug, group},
                       monsterImplementation(record->id).kind, command.position};
    simulation_->spawnEnemies(std::span<const MonsterSpawn>(&spawn, 1));
}

void GameSessionImpl::damageDebugMonster(const DebugDamageMonster &command) {
    if (state().player.actions.dead || !std::isfinite(command.amount) ||
        command.amount <= 0 || command.amount > 10000000.f) return;
    if (auto *enemy = simulation_->findEnemy(command.target); enemy && enemy->hp > 0)
        simulation_->damageEnemy(*enemy, command.amount, state().player.id, 0, true);
}
} // namespace d2x
