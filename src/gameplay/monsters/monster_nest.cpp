#include "gameplay/simulation/simulation.hpp"
#include <array>

namespace d2x {
std::optional<MonsterSpawn> Simulation::nestSpawn(
    Enemy &nest, std::span<const MonsterSpawn> queued) {
    const auto source = monsterNest_ ? monsterNest_(nest) : std::nullopt;
    const auto ai = monsterAi_ ? monsterAi_(nest) : std::nullopt;
    if (!source || !ai || ai->kind != MonsterAiKind::FoulCrowNest ||
        source->mode != "S1" || nest.aiLoop >= ai->params[2] ||
        state_.area.enemies.size() + queued.size() >= 65536) return std::nullopt;
    const std::array<Vec, 9> offsets{{{0, 3}, {1, 3}, {-1, 3}, {0, 2},
                                      {2, 2}, {-2, 2}, {2, 3}, {-2, 3}, {0, 4}}};
    for (const Vec offset : offsets) {
        const Vec position = nest.pos + offset;
        if (!grid_->walkable(position) ||
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
                                std::to_string(nest.aiLoop + 1);
        MonsterSpawn spawn{{source->child, {}, key, MonsterRank::Normal,
                            SpawnOrigin::Summoned, nest.identity.group},
                           MonsterKind::BloodHawk, position};
        ++nest.aiLoop;
        nest.aiWait = float(ai->params[0]) / 25.f;
        return spawn;
    }
    nest.aiWait = 20.f / 25.f;
    return std::nullopt;
}
} // namespace d2x
