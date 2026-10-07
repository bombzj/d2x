#include "game_host.hpp"
#include <cmath>
#include <limits>
#include <utility>

namespace d2x {
std::optional<PersistentCharacter> GameHost::exportCharacter(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    return slot ? slot->game->exportCharacter(binding.player) : std::nullopt;
}
const GameHost::Slot *GameHost::find(GameHandle handle) const {
    if (handle.slot >= slots_.size()) return nullptr;
    const auto &slot = slots_[size_t(handle.slot)];
    return slot.game && slot.generation == handle.generation ? &slot : nullptr;
}
GameHost::Slot *GameHost::find(GameHandle handle) {
    return const_cast<Slot *>(std::as_const(*this).find(handle));
}
PlayerBinding GameHost::create(server::GameDefinition definition) {
    if (nextGeneration_ == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Game generation space exhausted");
    auto game = std::make_unique<server::GameInstance>(std::move(definition));
    size_t index = 0;
    while (index < slots_.size() && slots_[index].game) ++index;
    if (index == slots_.size()) slots_.emplace_back();
    auto &slot = slots_[index];
    slot.game = std::move(game); slot.accumulator = 0; slot.paused = slot.resumed = false;
    slot.generation = nextGeneration_++;
    try { publish(index); }
    catch (...) { slot.game.reset(); slot.snapshots.clear(); throw; }
    return {{uint64_t(index), slot.generation}, {1}};
}
bool GameHost::destroy(GameHandle handle) {
    auto *slot = find(handle);
    if (!slot) return false;
    slot->game.reset(); slot->snapshots.clear(); slot->accumulator = 0;
    return true;
}
bool GameHost::pause(GameHandle handle, bool paused) {
    auto *slot = find(handle);
    if (!slot) return false;
    if (slot->paused != paused) {
        slot->paused = paused; slot->accumulator = 0;
        slot->resumed = !paused;
        if (paused) slot->game->suspend();
        publish(size_t(handle.slot));
    }
    return true;
}
void GameHost::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return;
    for (size_t index = 0; index < slots_.size(); ++index) {
        auto &slot = slots_[index];
        if (!slot.game || slot.paused) continue;
        if (!slot.resumed) slot.accumulator += seconds;
        slot.resumed = false;
        // Bound work per scheduler call, retaining debt instead of variable steps.
        bool changed = false;
        for (int steps = 0; slot.accumulator >= .04 && steps < 8; ++steps) {
            slot.game->step(); slot.accumulator -= .04;
            changed = true;
        }
        if (changed) publish(index);
    }
}
CommandStatus GameHost::submit(PlayerBinding binding, server::GameCommand command) {
    auto *slot = find(binding.game);
    if (!slot) return CommandStatus::InvalidBinding;
    if (slot->paused) return CommandStatus::Paused;
    return slot->game->enqueue(binding.player, std::move(command));
}
bool GameHost::step(GameHandle handle, uint32_t frames) {
    auto *slot = find(handle);
    if (!slot || !slot->paused || !frames || frames > 250) return false;
    for (uint32_t i = 0; i < frames; ++i) slot->game->step();
    slot->accumulator = 0;
    publish(size_t(handle.slot));
    return true;
}
void GameHost::publish(size_t index) {
    auto &slot = slots_[index];
    std::map<PlayerId, std::shared_ptr<const PlayerSnapshot>> snapshots;
    for (const auto player : slot.game->participants()) {
        auto snapshot = slot.game->snapshot(player);
        if (!snapshot) throw std::logic_error("Participant snapshot is missing");
        snapshot->game = {uint64_t(index), slot.generation}; snapshot->paused = slot.paused;
        snapshots.emplace(player, std::make_shared<const PlayerSnapshot>(std::move(*snapshot)));
    }
    slot.snapshots = std::move(snapshots);
}
std::shared_ptr<const PlayerSnapshot> GameHost::read(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    if (!slot) return {};
    const auto found = slot->snapshots.find(binding.player);
    return found == slot->snapshots.end() ? nullptr : found->second;
}
std::optional<server::SystemSteps> GameHost::systemSteps(GameHandle handle) const {
    const auto *slot = find(handle);
    return slot ? std::optional{slot->game->systemSteps()} : std::nullopt;
}
std::optional<std::vector<server::EventBatch>> GameHost::pendingEvents(GameHandle handle) const {
    const auto *slot = find(handle);
    if (!slot) return {};
    const auto &events = slot->game->pendingEvents();
    return std::vector<server::EventBatch>{events.begin(), events.end()};
}
bool GameHost::acknowledgeEvents(GameHandle handle, uint64_t sequence) {
    auto *slot = find(handle);
    return slot && slot->game->acknowledgeEvents(sequence);
}
std::optional<std::vector<server::world::PrepareArea>> GameHost::pendingAreas(GameHandle handle) const {
    const auto *slot = find(handle);
    if (!slot) return {};
    return slot->game->pendingAreas();
}
server::DomainResult<> GameHost::installArea(GameHandle handle, server::world::PreparedArea area) {
    auto *slot = find(handle);
    if (!slot) return {server::DomainStatus::Stale, std::nullopt};
    return slot->game->installArea(std::move(area));
}
} // namespace d2x
