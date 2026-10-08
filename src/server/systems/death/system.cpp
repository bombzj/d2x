#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/rewards/experience.hpp"
namespace d2x::server::death {
DomainResult<> System::execute(const ActorContext &, const Request &) {
    // Corpse inventory, native death penalties and resurrection remain one
    // future transaction. Do not acknowledge a free revival or destroy items.
    return {};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered || player.persistent.player.hp > 0) continue;
        if (!state_.transitions.contains(player.actor)) {
            ports_.skills.cancel(id, player.actor);
            const auto duration = player.rules.character ? player.rules.character->deathTicks : 0;
            state_.transitions.emplace(player.actor, Transition{tick.tick + 1, player.actor, {}, true, tick.tick + uint64_t(duration)});
        }
    }
    std::erase_if(state_.transitions, [&](const auto &entry) {
        for (const auto &[id, player] : ports_.players.all()) { (void)id; if (player.actor == entry.first) return false; }
        return true;
    });
    for (const auto &[id, monster] : ports_.monsters.read().actors) {
        if (monster.life > 0 || monster.rewardComplete) continue;
        auto reward = state_.rewards.find(id);
        if (reward == state_.rewards.end()) {
            const PlayerState *killer = nullptr;
            for (const auto &[playerId, player] : ports_.players.all()) {
                (void)playerId; if (player.actor == monster.killer) { killer = &player; break; }
            }
            if (!killer || !killer->entered || killer->area != monster.area || killer->persistent.player.hp <= 0 ||
                killer->persistent.player.level < 1 || size_t(killer->persistent.player.level) >= monster.rule.experience.size()) {
                ports_.monsters.rewardComplete(id); continue;
            }
            const auto amount = playerExperienceGain(monster.rule.experience[size_t(killer->persistent.player.level)], killer->totals.character.combat.experiencePercent);
            if (!amount) { ports_.monsters.rewardComplete(id); continue; }
            // Capture once: backpressure must not reroll rewards using a later
            // level, equipment set, or player that reused an old identifier.
            reward = state_.rewards.emplace(id, Reward{killer->player, killer->actor, amount}).first;
        }
        const auto *killer = ports_.players.find(reward->second.player);
        if (!killer || !killer->entered || killer->actor != reward->second.actor || killer->persistent.player.hp <= 0 || killer->lastExperienceAward == UINT64_MAX) {
            ports_.monsters.rewardComplete(id); state_.rewards.erase(reward); continue;
        }
        const auto result = ports_.progression.award({killer->player, killer->lastExperienceAward + 1, reward->second.amount, tick.tick});
        if (result || result.status == DomainStatus::Conflict) {
            ports_.monsters.rewardComplete(id); state_.rewards.erase(reward);
        } else blocked = true;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
