#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <cmath>

namespace d2x {
std::optional<MonsterSpawn> Simulation::nestSpawn(
    Enemy &nest, std::span<const MonsterSpawn> queued) {
    const auto source = monsterNest_ ? monsterNest_(nest) : std::nullopt;
    const auto ai = monsterAi_ ? monsterAi_(nest) : std::nullopt;
    const auto origin = nest.nestSpawnPosition;
    nest.nestSpawnPosition.reset();
    if (!source || !ai || !origin || ai->kind != MonsterAiKind::FoulCrowNest ||
        source->mode != "S1" ||
        state_.area.enemies.size() + queued.size() >= 65536) return std::nullopt;
    Enemy child;
    child.identity.monster = source->child;
    constexpr int radius = 3;
    int offsetX = 0, offsetY = 0;
    if (monsterAiRandom(nest) & 1) {
        offsetY = radius;
        offsetX = int(monsterAiRandom(nest) % radius);
    } else {
        offsetX = radius;
        offsetY = int(monsterAiRandom(nest) % radius);
    }
    if (monsterAiRandom(nest) & 1) offsetX = -offsetX;
    if (monsterAiRandom(nest) & 1) offsetY = -offsetY;
    for (int attempt = 0; attempt < 8 * radius; ++attempt) {
        const Vec position = *origin + Vec{float(offsetX), float(offsetY)};
        if (offsetX == -radius && offsetY < radius) ++offsetY;
        else if (offsetY == radius && offsetX < radius) ++offsetX;
        else if (offsetX == radius && offsetY > -radius) --offsetY;
        else --offsetX;
        if (!grid_->walkable(position, spawnRule(child)) ||
            (position - state_.player.pos).length() < .8f) continue;
        bool occupied = false;
        for (const auto &other : state_.area.enemies)
            if (other.hp > 0 && (position - other.pos).length() < .8f) {
                occupied = true;
                break;
            }
        for (const auto &pending : queued)
            if ((position - pending.position).length() < .8f) occupied = true;
        if (occupied) continue;
        const std::string key = "summon." + std::to_string(nest.id.value) + "." +
                                std::to_string(nest.aiLoop);
        MonsterSpawn spawn{{source->child, {}, key, MonsterRank::Normal,
                            SpawnOrigin::Summoned, nest.identity.group},
                           MonsterKind::BloodHawk, position};
        return spawn;
    }
    return std::nullopt;
}
} // namespace d2x
