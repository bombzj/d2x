#include "game_instance.hpp"
#include "movement.hpp"
#include "player_projection.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server {
GameInstance::GameInstance(GameDefinition definition)
    : settings_(definition.settings), areas_(std::move(definition.area)),
      entities_(definition.persistent.nextEntityId), random_(initialRandom(settings_.mapSeed)),
      rulesFingerprint_(definition.rulesFingerprint), rules_(std::move(definition.rules)),
      systems_(players_, areas_, entities_, random_, events_, rules_, settings_) {
    if (!rulesFingerprint_) throw std::runtime_error("Game admission requires prepared rules");
    systems_.world.initialize();
    players_.admit({1}, std::move(definition.character), std::move(definition.persistent), areas_, rules_);
}
PlayerId GameInstance::admit(GameDefinition definition) {
    if (players_.all().size() >= 8 || nextPlayer_ == UINT64_MAX)
        throw std::runtime_error("Game participant capacity exhausted");
    if (definition.rulesFingerprint != rulesFingerprint_ || definition.settings.mapSeed != settings_.mapSeed ||
        definition.settings.difficulty != settings_.difficulty || definition.persistent.player.id.value < entities_.cursor() ||
        definition.persistent.nextEntityId <= definition.persistent.player.id.value || definition.persistent.nextEntityId > UINT32_MAX)
        throw std::runtime_error("Invalid participant admission rules or identity range");
    const PlayerId id{nextPlayer_};
    players_.admit(id, std::move(definition.character), std::move(definition.persistent), areas_, definition.rules);
    entities_ = EntityIds(players_.find(id)->persistent.nextEntityId);
    ++nextPlayer_; ++revision_;
    return id;
}
bool GameInstance::enter(PlayerId id, bool active) {
    auto found = players_.players_.find(id);
    if (found == players_.players_.end()) return false;
    found->second.entered = active;
    if (!active) {
        std::erase_if(commands_, [&](const auto &pending) { return pending.player == id; });
        found->second.route.clear(); found->second.moving = false; systems_.travel.cancel(id);
        systems_.skills.cancel(id, found->second.actor);
    }
    ++revision_;
    return true;
}
bool GameInstance::remove(PlayerId id) {
    if (!players_.find(id)) return false;
    std::erase_if(commands_, [&](const auto &pending) { return pending.player == id; });
    systems_.travel.cancel(id); systems_.skills.cancel(id, players_.find(id)->actor);
    players_.players_.erase(id); ++revision_;
    return true;
}
std::vector<RegionId> GameInstance::visibleAreas(PlayerId id) const {
    return systems_.world.visible(id);
}
std::vector<PlayerId> GameInstance::visiblePlayers(PlayerId id) const {
    return systems_.replication.visible(id);
}
std::optional<PersistentCharacter> GameInstance::exportCharacter(PlayerId id) const {
    const auto *player = players_.find(id);
    if (!player) return {};
    auto result = player->persistent;
    result.nextEntityId = entities_.cursor();
    result.lastRegion = player->area;
    return result;
}
std::optional<PersistentCharacter> GameInstance::publicEquipment(PlayerId id) const {
    const auto *player = players_.find(id); if (!player || !player->entered) return {};
    const auto &saved = player->persistent;
    PersistentCharacter projection;
    projection.player.id = player->actor; projection.player.name = saved.player.name;
    projection.player.characterClass = saved.player.characterClass; projection.player.level = saved.player.level;
    projection.player.weaponSet = saved.player.weaponSet;
    projection.containers = saved.containers;
    for (const auto container : {saved.containers.equipment, saved.containers.beltEquipment})
        if (const auto found = saved.inventory.containers.find(container); found != saved.inventory.containers.end())
            projection.inventory.containers.emplace(container, found->second);
    for (const auto &[key, item] : saved.inventory.items) {
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (location && projection.inventory.containers.contains(location->container)) projection.inventory.items.emplace(key, item);
    }
    return projection;
}
std::vector<MonsterSnapshot> GameInstance::visibleMonsters(PlayerId id) const {
    return systems_.replication.visibleMonsters(id, tick_);
}
CommandStatus GameInstance::enqueue(PlayerId id, GameCommand command) {
    const auto sequence = command.sequence;
    const auto result = [&]() -> CommandStatus {
    const auto found = players_.players_.find(id);
    if (found == players_.players_.end()) return CommandStatus::InvalidBinding;
    auto &player = found->second;
    if (!player.entered) return CommandStatus::Unavailable;
    if (command.area != player.area || command.areaGeneration != areas_.at(player.area).generation ||
        command.sequence <= player.acceptedSequence) return CommandStatus::Stale;
    // Readiness is explicit. Merely declaring a handler must not make a stub
    // advance acceptedSequence or pretend that gameplay was queued/applied.
    if (!acceptsCommand(command.payload)) return CommandStatus::NotImplemented;
    if (commands_.size() >= 256) return CommandStatus::QueueFull;
    const auto sequence = command.sequence;
    commands_.push_back({id, std::move(command)});
    player.acceptedSequence = sequence;
    return CommandStatus::Queued;
    }();
    observeCommand(id, sequence, result);
    return result;
}
void GameInstance::step() {
    for (const auto &pending : commands_) {
        auto &player = players_.players_.at(pending.player);
        const auto &command = pending.command;
        const auto &area = areas_.at(player.area);
        const ActorContext actor{pending.player, player.actor, player.area,
            area.generation, command.sequence, tick_};
        // Recheck at execution: earlier commands may have replaced the area.
        auto result = CommandStatus::Unavailable;
        if (player.entered) {
            if (command.area != player.area || command.areaGeneration != area.generation) result = CommandStatus::Stale;
            else {
                if (commandSystem(command.payload) == SystemId::Movement || commandSystem(command.payload) == SystemId::Travel)
                    player.locomotionSequence = command.sequence;
                const auto domain = commandSystem(command.payload);
                if (domain == SystemId::Movement || domain == SystemId::Travel) systems_.skills.cancel(player.player, player.actor);
                if (const auto *skill = std::get_if<skills::Request>(&command.payload); skill && skill->action == skills::Action::Cast) {
                    systems_.travel.cancel(player.player); player.locomotionSequence = command.sequence;
                }
                if (domain == SystemId::Inventory && systems_.skills.busy(player.actor, tick_)) result = CommandStatus::Conflict;
                else result = dispatchCommand(actor, command.payload, systems_);
            }
        }
        player.result = {command.sequence, result};
        observeCommand(pending.player, command.sequence, result);
        if (commandSystem(command.payload) == SystemId::Movement)
            player.movementSequence = command.sequence;
    }
    commands_.clear();
    simulation_.step({tick_}, systems_);
    ++tick_; ++revision_;
}
void GameInstance::suspend() {
    for (const auto &pending : commands_) {
        auto &player = players_.players_.at(pending.player);
        player.result = {pending.command.sequence, CommandStatus::Paused};
        observeCommand(pending.player, pending.command.sequence, CommandStatus::Paused);
        if (commandSystem(pending.command.payload) == SystemId::Movement)
            player.movementSequence = pending.command.sequence;
    }
    commands_.clear();
    for (const auto &[id, player] : players_.all()) { systems_.travel.cancel(id); systems_.skills.cancel(id, player.actor); }
    systems_.movement.suspend();
    ++revision_;
}
std::optional<PlayerSnapshot> GameInstance::snapshot(PlayerId id) const {
    const auto *player = players_.find(id);
    if (!player) return {};
    auto result = projectPlayer(*player);
    result.tick = tick_; result.revision = revision_;
    result.rulesFingerprint = rulesFingerprint_;
    result.areaGeneration = areas_.at(player->area).generation;
    const auto cast = systems_.skills.read().casts.find(player->actor);
    result.attacking = cast != systems_.skills.read().casts.end() && !cast->second.interrupted && cast->second.until > tick_;
    const auto death = systems_.death.read().transitions.find(player->actor);
    result.deadSettled = death != systems_.death.read().transitions.end() && death->second.ready <= tick_;
    return result;
}
std::vector<PlayerId> GameInstance::participants() const {
    std::vector<PlayerId> result;
    result.reserve(players_.all().size());
    for (const auto &[id, player] : players_.all()) {
        (void)player;
        result.push_back(id);
    }
    return result;
}
DomainResult<> GameInstance::installArea(world::PreparedArea area) {
    return systems_.world.install(std::move(area));
}
DomainResult<> GameInstance::grantExperience(PlayerId id, uint64_t amount) {
    const auto *player = players_.find(id);
    if (!player) return {DomainStatus::InvalidActor, {}};
    if (player->lastExperienceAward == UINT64_MAX) return {DomainStatus::Capacity, {}};
    return systems_.progression.award({id, player->lastExperienceAward + 1, amount, tick_});
}

} // namespace d2x::server
