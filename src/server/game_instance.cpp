#include "game_instance.hpp"
#include "movement.hpp"
#include "player_projection.hpp"
#include "core/random.hpp"

namespace d2x::server {
GameInstance::GameInstance(GameDefinition definition)
    : settings_(definition.settings), areas_(std::move(definition.area)),
      entities_(definition.persistent.nextEntityId), random_(initialRandom(settings_.mapSeed)),
      rulesFingerprint_(definition.rulesFingerprint), rules_(std::move(definition.rules)),
      systems_(players_, areas_, entities_, random_, events_, rules_, settings_) {
    if (!rulesFingerprint_) throw std::runtime_error("Game admission requires prepared rules");
    players_.admit({1}, std::move(definition.character), std::move(definition.persistent), areas_);
}
std::optional<PersistentCharacter> GameInstance::exportCharacter(PlayerId id) const {
    const auto *player = players_.find(id);
    if (!player) return {};
    auto result = player->persistent;
    result.nextEntityId = entities_.cursor();
    result.lastRegion = player->area;
    return result;
}
CommandStatus GameInstance::enqueue(PlayerId id, GameCommand command) {
    const auto found = players_.players_.find(id);
    if (found == players_.players_.end()) return CommandStatus::InvalidBinding;
    auto &player = found->second;
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
}
void GameInstance::step() {
    for (const auto &pending : commands_) {
        auto &player = players_.players_.at(pending.player);
        const auto &command = pending.command;
        const auto &area = areas_.at(player.area);
        const ActorContext actor{pending.player, player.actor, player.area,
            area.generation, command.sequence, tick_};
        // Recheck at execution: earlier commands may have replaced the area.
        const auto result = command.area != player.area || command.areaGeneration != area.generation ? CommandStatus::Stale
            : dispatchCommand(actor, command.payload, systems_);
        player.result = {command.sequence, result};
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
        if (commandSystem(pending.command.payload) == SystemId::Movement)
            player.movementSequence = pending.command.sequence;
    }
    commands_.clear();
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
} // namespace d2x::server
