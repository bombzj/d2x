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
    return systems_.population.admit({monster.identity, monster.implementation, player->area, monster.position, true, monster.rule,monster.skillPositions});
}
std::optional<DiagnosticSnapshot> GameInstance::diagnostics(PlayerId id, size_t limit, uint64_t since,
    uint64_t commandSince, std::optional<Vec> destination) const {
    const auto *player = players_.find(id);
    if (!player) return {};
    limit = std::clamp<size_t>(limit, 1, 256);
    DiagnosticSnapshot result;
    result.tick = tick_; result.player = *snapshot(id); result.record = player->persistent.player;
    result.containers = player->persistent.containers;
    result.waypoints=player->persistent.waypoints; result.denRemaining=systems_.quests.read().denRemaining; result.denCleared=systems_.quests.read().denCleared; result.portals=visiblePortals(id);
    result.corpses=player->persistent.corpses; result.merchantDeferred=systems_.merchant.read().deferred;
    result.craftingPending=systems_.crafting.read().pending.size();result.craftingDeferred=systems_.crafting.read().deferred;
    for(const auto &[key,object]:systems_.objects.read().objects) { (void)key; if(object.area==player->area && result.objects.size()<limit) result.objects.push_back(object); }
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
    result.lootPending = systems_.loot.read().pending.size(); result.lootDeferred = systems_.loot.read().deferred;
    result.itemTriggersPending=systems_.effects.pendingItemTriggers();result.itemTriggersDeferred=systems_.effects.read().itemDeferred;
    result.chargedSkills=player->totals.chargedSkills;
    if (const auto found = systems_.effects.read().players.find(player->actor); found != systems_.effects.read().players.end()) {
        result.healingQueued = found->second.healing.size(); result.manaQueued = found->second.mana.size();
        for (const auto &effect : found->second.states.entries()) result.effects.push_back({effect.spec.state.id, effect.expiresAt.value_or(0)});
    }
    result.itemCount = player->persistent.inventory.items.size();
    for (const auto &[key, item] : player->persistent.inventory.items) {
        if (result.items.size() >= limit) break;
        result.items.push_back({key, item.revision, item.definition, item.location, item.quantity, item.durability});
    }
    for (const auto &[key, item] : systems_.items.read().world.items) {
        const auto &at = std::get<GroundLocation>(item.location);
        if (at.region != player->area) continue;
        ++result.itemCount;
        if (result.items.size() < limit) result.items.push_back({key, item.revision, item.definition, item.location, item.quantity, item.durability});
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
        value.chilledUntil=monster.chilledUntil;value.frozenUntil=monster.frozenUntil;value.knockedUntil=monster.knockedUntil;value.nextHitTick=monster.nextHitTick;value.owner=monster.owner;
        value.rules=monster.rule.ai;value.damageRegen=monster.rule.damageRegen;value.threat=monster.rule.threat;
        value.nativeVelocity=monster.rule.nativeVelocity;value.movementMask=monster.rule.collision.mask;value.spawnMask=monster.rule.spawnCollision.mask;
        value.blockChance=monster.rule.blockChance;value.shield=monster.shield;value.nestSpawned=monster.nestSpawned;value.webUntil=monster.webUntil;
        value.interruption=monster.interruption;value.corpseUnavailable=monster.corpseUnavailable;
        value.components=monster.components;value.componentCounts=monster.rule.componentCounts;
        value.identity=monster.identity;value.enchantment=monster.rule.enchantment;value.home=monster.home;value.skillPositions=monster.skillPositions;
        if(const auto region=systems_.monsters.read().componentPalettes.find(monster.area);region!=systems_.monsters.read().componentPalettes.end())
            if(const auto palette=region->second.find(monster.rule.nativeClass);palette!=region->second.end()) value.componentVariants=palette->second.size();
        for(const auto &[mode,rule]:monster.rule.attacks) {(void)rule;value.attacks.push_back(mode);}
        for(const auto &[skill,rule]:monster.rule.skillActions) {(void)rule;value.skills.push_back(skill);}
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

namespace d2x::server {
DomainResult<> GameInstance::grantGold(PlayerId id,uint32_t amount) {
    const auto *p=players_.find(id); if(!p || !p->entered || p->persistent.player.hp<=0 || !amount) return {DomainStatus::InvalidActor,{}};
    auto record=p->persistent.player; const auto cap=unsigned(record.level)*10000;
    if(record.gold>cap || amount>cap-record.gold) return {DomainStatus::Capacity,{}};
    record.gold+=amount;
    const ActorContext actor{id,p->actor,p->area,areas_.at(p->area).generation,0,tick_};
    auto plan=systems_.transactions.prepare(transactions::CharacterEdit{actor,p->inventoryRevision,p->characterRevision,std::move(record)});
    if(!plan) return {plan.status,{}};
    return systems_.transactions.commit(std::move(*plan.value));
}
DomainResult<> GameInstance::missileHit(PlayerId id,EntityId source,uint32_t amount,DamageType type,bool returnFire) {
    const auto *p=players_.find(id);const auto *m=systems_.monsters.find(source);
    if(!p || !p->entered || !m || m->owner || m->life<=0 || m->area!=p->area || !amount || type==DamageType::Poison || amount>INT32_MAX/256) return {DomainStatus::InvalidActor,{}};
    if(!systems_.effects.reactionCapacity()) return {DomainStatus::Capacity,{}};
    const ActorContext actor{id,p->actor,p->area,areas_.at(p->area).generation,0,tick_};
    const auto result=systems_.effects.receive(actor,int64_t(amount)*256,type);
    if(result) systems_.effects.react(actor,source,CombatEffectEvent::HitByMissile,returnFire);
    return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<> GameInstance::damagePlayer(PlayerId id,uint32_t amount) {
    const auto *p=players_.find(id); if(!p || !amount) return {DomainStatus::InvalidActor,{}};
    const auto received=systems_.effects.receive({id,p->actor,p->area,areas_.at(p->area).generation,0,tick_},int64_t(amount)*256,DamageType::Physical);
    return {received.status,received?std::optional{std::monostate{}}:std::nullopt};
}
}

namespace d2x::server {
DomainResult<> GameInstance::damageMonster(PlayerId id, EntityId target, std::optional<uint32_t> amount) {
    const auto *player = players_.find(id);
    const auto *monster = systems_.monsters.find(target);
    if (!player || !player->entered || player->persistent.player.hp <= 0 || !monster ||
        monster->area != player->area || monster->life <= 0 || (amount && !*amount))
        return {DomainStatus::InvalidActor, {}};
    return systems_.monsters.damage(target, player->actor, amount ? int64_t(*amount) * 256 : monster->life, tick_);
}
}
