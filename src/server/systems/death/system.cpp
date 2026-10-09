#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include "server/systems/monsters/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/rewards/experience.hpp"
#include "server/systems/loot/system.hpp"
namespace d2x::server::death {
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration) return {DomainStatus::InvalidActor, {}};
    if (request.action == Action::RecoverCorpse) {
        if (!request.corpse || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
        const auto &corpses = player->persistent.corpses;
        const auto found = std::find_if(corpses.begin(), corpses.end(), [&](const auto &corpse) { return corpse.id == request.corpse->id && corpse.owner == actor.actor && corpse.region == actor.area; });
        if (found == corpses.end() || meleeDistance(player->position, 2, found->position, 2) > 50) return {DomainStatus::InvalidRequest, {}};
        ports_.skills.cancel(actor.player, actor.actor);
        if (meleeDistance(player->position, 2, found->position, 2) <= 8) {
            const auto result = recover(actor, found->id);
            if (result.status != DomainStatus::Capacity || ports_.transactions.hasOutputCapacity()) return result;
            state_.recoveries.insert_or_assign(actor.player, Recovery{actor, found->id, player->locomotionSequence});
            return {DomainStatus::Applied, std::monostate{}};
        }
        if (ports_.movement.execute(actor, {MovementAction::Move, found->position, player->running}) != CommandStatus::Applied) return {DomainStatus::Conflict, {}};
        state_.recoveries.insert_or_assign(actor.player, Recovery{actor, found->id, player->locomotionSequence});
        return {DomainStatus::Applied, std::monostate{}};
    }
    const auto death = state_.transitions.find(actor.actor);
    if (player->persistent.player.hp > 0 || death == state_.transitions.end() || !death->second.finalized || actor.tick < death->second.ready) return {DomainStatus::Conflict, {}};
    if (!death->second.companionsSettled) {
        const auto result = ports_.companions.ownerDied(actor);
        if (!result) return result;
        death->second.companionsSettled = true;
    }
    death->second.reviving = true;
    const auto townId = area->definition.townRegion;
    const auto *town = ports_.areas.find(townId);
    if (!town) {
        const auto prepared = ports_.world.requestTown(actor);
        if (!prepared) { if (prepared.status != DomainStatus::Capacity) death->second.reviving = false; return {prepared.status, {}}; }
        return {DomainStatus::Applied, std::monostate{}};
    }
    auto record = player->persistent.player; record.hp = 1;
    transactions::CharacterEdit edit{actor, player->inventoryRevision, player->characterRevision, std::move(record), transactions::ResourceRefresh::LevelUp};
    edit.revival = PointTarget{townId, town->generation, town->definition.spawn};
    auto plan = ports_.transactions.prepare(std::move(edit));
    if (!plan) { if (plan.status == DomainStatus::Capacity) return {DomainStatus::Applied, std::monostate{}}; death->second.reviving = false; return {plan.status, {}}; }
    const auto result = ports_.transactions.commit(std::move(*plan.value));
    if (result) { state_.transitions.erase(death); state_.recoveries.erase(actor.player); }
    else if (result.status == DomainStatus::Capacity) return {DomainStatus::Applied, std::monostate{}};
    else death->second.reviving = false;
    return result;
}
void System::advanceRecovery(TickContext tick) {
    for (auto pending = state_.recoveries.begin(); pending != state_.recoveries.end();) {
        const auto request = pending->second;
        const auto *player = ports_.players.find(pending->first);
        const auto *area = ports_.areas.find(request.actor.area);
        if (!player || !player->entered || player->actor != request.actor.actor || player->persistent.player.hp <= 0 || player->area != request.actor.area || !area || area->generation != request.actor.areaGeneration || player->locomotionSequence != request.locomotion) { pending = state_.recoveries.erase(pending); continue; }
        const auto found = std::find_if(player->persistent.corpses.begin(), player->persistent.corpses.end(), [&](const auto &corpse) { return corpse.id == request.corpse; });
        if (found == player->persistent.corpses.end()) { pending = state_.recoveries.erase(pending); continue; }
        const bool reached = meleeDistance(player->position, 2, found->position, 2) <= 8;
        if (!reached && !player->route.empty()) { ++pending; continue; }
        if (reached) {
            auto actor = request.actor; actor.tick = tick.tick;
            const auto result = recover(actor, request.corpse);
            if (result.status == DomainStatus::Capacity && !ports_.transactions.hasOutputCapacity()) { ++pending; continue; }
            if (result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
        }
        pending = state_.recoveries.erase(pending);
    }
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered || player.persistent.player.hp > 0) continue;
        if (!state_.transitions.contains(player.actor)) {
            ports_.skills.cancel(id, player.actor);
            const auto duration = player.rules.character ? player.rules.character->deathTicks : 0;
            state_.transitions.emplace(player.actor, Transition{tick.tick + 1, player.actor, {}, false, tick.tick + uint64_t(duration), false});
        }
        const ActorContext actor{id, player.actor, player.area, ports_.areas.at(player.area).generation, 0, tick.tick};
        auto &transition = state_.transitions.at(player.actor);
        if (!transition.finalized) {
            const auto result = settle(actor); transition.finalized = bool(result); blocked |= !result;
        }
        if (transition.finalized && tick.tick >= transition.ready && !transition.companionsSettled) {
            const auto result = ports_.companions.ownerDied(actor);
            transition.companionsSettled = bool(result);
            blocked |= !result;
        }
        if (transition.reviving) execute(actor, {Action::Resurrect, {}});
    }
    advanceRecovery(tick);
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

            // Capture once: backpressure must not reroll rewards using a later
            // level, equipment set, or player that reused an old identifier.
            reward = state_.rewards.emplace(id, Reward{killer->player, killer->actor, amount,false,killer->totals.character.combat.lifeOnKill,killer->totals.character.combat.manaOnKill,false,killer->persistent.player}).first;
            reward->second.hireling=ports_.companions.experience(killer->player,monster);
        }
        const auto *killer = ports_.players.find(reward->second.player);
        if (!killer || !killer->entered || killer->actor != reward->second.actor || killer->persistent.player.hp <= 0 || killer->lastExperienceAward == UINT64_MAX) {
            ports_.monsters.rewardComplete(id); state_.rewards.erase(reward); continue;
        }
        if(!reward->second.restored) {
            const ActorContext actor{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
            const auto result=ports_.transactions.resources(actor,killer->characterRevision,
                std::clamp(killer->persistent.player.hp+float(reward->second.life),0.f,float(killer->totals.character.maxLife)),
                std::clamp(killer->persistent.player.mana+float(reward->second.mana),0.f,float(killer->totals.character.maxMana)),killer->persistent.player.stamina);
            if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            if(!result) {ports_.monsters.rewardComplete(id);state_.rewards.erase(reward);continue;}
            reward->second.restored=true;
        }
        if(!reward->second.hirelingAwarded) {
            if(reward->second.hireling) {
                const ActorContext actor{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
                const auto result=ports_.companions.awardExperience(actor,*reward->second.hireling);
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            }
            reward->second.hirelingAwarded=true;
        }
        if (!reward->second.lootQueued) {
            LootRequest source; source.source = id; source.identity = monster.identity; source.region = monster.area; source.difficulty = monster.rule.difficulty; source.sourceSeed = true; source.rewardModifiers=monsterRewardModifiers(monster.rule.enchantment);
            source.monsterPlayerCount=monster.admittedPlayerCount;
            const auto queued = ports_.loot.queue({monster.deathOccurrence, source, killer->player, monster.position,{},reward->second.lootClaimant});
            if (!queued) { blocked = true; continue; }
            reward->second.lootQueued = true;
        }
        if (!reward->second.amount) { ports_.monsters.rewardComplete(id); state_.rewards.erase(reward); continue; }
        const auto result = ports_.progression.award({killer->player, killer->lastExperienceAward + 1, reward->second.amount, tick.tick});
        if (result || result.status == DomainStatus::Conflict) {
            ports_.monsters.rewardComplete(id); state_.rewards.erase(reward);
        } else blocked = true;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
