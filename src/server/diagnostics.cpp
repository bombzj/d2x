#include "game_instance.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server {
void GameInstance::observeCommand(PlayerId player, uint64_t sequence, CommandStatus status) noexcept {
    if (commandHistoryNext_ == UINT64_MAX) return;
    const auto index = commandHistoryNext_++;
    commandHistory_[(index - 1) % commandHistory_.size()] = {index, tick_, player, {sequence, status}};
}
DomainResult<> GameInstance::restoreResources(PlayerId id) {
    const auto *player = players_.find(id);
    if (!player || !player->entered || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    auto record = player->persistent.player;
    record.hp = float(player->totals.character.maxLife);
    record.mana = float(player->totals.character.maxMana);
    record.stamina = float(player->totals.character.maxStamina);
    const ActorContext actor{id, player->actor, player->area, areas_.at(player->area).generation, 0, tick_};
    auto plan = systems_.transactions.prepare(transactions::CharacterEdit{actor, player->inventoryRevision,
        player->characterRevision, std::move(record), transactions::ResourceRefresh::Clamp, 0});
    if (!plan) return {plan.status, {}};
    return systems_.transactions.commit(std::move(*plan.value));
}
DomainResult<EntityId> GameInstance::spawnMonster(PlayerId id, const PreparedMonster &monster) {
    const auto *player = players_.find(id);
    if (!player || !player->entered || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    return systems_.population.admit({monster.identity, monster.implementation, player->area, monster.position, true, monster.rule});
}
std::optional<DiagnosticSnapshot> GameInstance::diagnostics(PlayerId id, size_t limit, uint64_t since,
    uint64_t commandSince, std::optional<Vec> destination) const {
    const auto *player = players_.find(id);
    if (!player) return {};
    limit = std::clamp<size_t>(limit, 1, 256);
    DiagnosticSnapshot result;
    result.tick = tick_; result.player = *snapshot(id); result.record = player->persistent.player;
    result.containers = player->persistent.containers;
    const auto &area = areas_.at(player->area);
    result.area = {area.definition, area.generation};
    for (const auto &[key, value] : areas_.all()) {
        (void)key; result.areas.push_back({value.definition, value.generation});
    }
    result.commandsQueued = commands_.size(); result.eventsQueued = events_.pending().size();
    result.eventFirst = events_.historyFirst(); result.eventLast = events_.historyLast();
    result.events = events_.history(since, limit);
    result.commandFirst = commandHistoryNext_ > commandHistory_.size() ? commandHistoryNext_ - commandHistory_.size() : 1;
    result.commandLast = commandHistoryNext_ - 1;
    if (commandSince != UINT64_MAX) for (auto sequence = std::max(result.commandFirst, commandSince + 1);
        sequence < commandHistoryNext_ && result.commands.size() < limit; ++sequence)
        result.commands.push_back(commandHistory_[(sequence - 1) % commandHistory_.size()]);
    result.itemCount = player->persistent.inventory.items.size();
    for (const auto &[key, item] : player->persistent.inventory.items) {
        if (result.items.size() >= limit) break;
        result.items.push_back({key, item.revision, item.definition, item.location, item.quantity, item.durability});
    }
    std::vector<const monsters::Actor *> nearby;
    for (const auto &[key, monster] : systems_.monsters.read().actors) {
        (void)key;
        if (monster.area == player->area) nearby.push_back(&monster);
    }
    result.monsterCount = nearby.size();
    std::sort(nearby.begin(), nearby.end(), [&](const auto *first, const auto *second) {
        const auto a = (first->position - player->position).length(), b = (second->position - player->position).length();
        return a != b ? a < b : first->id.value < second->id.value;
    });
    for (const auto *entry : nearby) {
        if (result.monsters.size() >= limit) break;
        const auto &monster = *entry;
        const auto key = monster.id;
        DiagnosticMonster value{key, monster.identity.monster, monster.area, monster.position, monster.life, monster.maximumLife,
            monster.revision, monster.busyUntil, monster.moving, monster.running, monster.rewardComplete, {}};
        if (const auto found = systems_.ai.read().controllers.find(key); found != systems_.ai.read().controllers.end()) value.controller = found->second;
        result.monsters.push_back(std::move(value));
    }
    for (const auto &[key, cast] : systems_.skills.read().casts) {
        (void)key; if (cast.area == player->area && result.casts.size() < limit) result.casts.push_back(cast);
    }
    result.pendingReleases = systems_.skills.pendingReleases();
    for (const auto &[key, missile] : systems_.missiles.read().missiles) {
        (void)key; if (missile.area != player->area) continue;
        ++result.missileCount;
        if (result.missiles.size() < limit) result.missiles.push_back(missile);
    }
    for (const auto &damage : systems_.combat.read().pending)
        if (damage.area == player->area && result.damage.size() < limit) result.damage.push_back(damage);
    result.spellImpacts = systems_.combat.read().spells.size(); result.spellTargets = systems_.combat.read().spellTargets;
    if (const auto found = systems_.travel.read().transitions.find(id); found != systems_.travel.read().transitions.end()) result.travel = found->second;
    if (destination && std::isfinite(destination->x) && std::isfinite(destination->y)) {
        const auto path = area.definition.collision.path(player->position, *destination - area.definition.origin, false, playerMovement);
        for (const auto point : path) {
            if (result.path.size() >= 1024) break;
            result.path.push_back(point + area.definition.origin);
        }
    }
    return result;
}
}
