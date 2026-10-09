#include "game_host.hpp"
#include <cmath>
#include <limits>
#include <utility>

namespace d2x {
std::set<size_t> GameHost::usedUniques(GameHandle handle) const {const auto *slot=find(handle);return slot?slot->game->usedUniques():std::set<size_t>{};}
std::optional<server::inventory::InputState> GameHost::inventoryInput(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    return slot ? slot->game->inventoryInput(binding.player) : std::nullopt;
}
std::optional<PersistentCharacter> GameHost::exportCharacter(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    return slot ? slot->game->exportCharacter(binding.player) : std::nullopt;
}
std::optional<PersistentCharacter> GameHost::publicEquipment(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    return slot ? slot->game->publicEquipment(binding.player) : std::nullopt;
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
    if (index == slots_.size()) {
        if (slots_.size() >= 256) throw std::runtime_error("Host game capacity exhausted");
        slots_.emplace_back();
    }
    auto &slot = slots_[index];
    slot.game = std::move(game); slot.accumulator = 0; slot.paused = slot.resumed = false; slot.debugPaused.reset();
    slot.generation = nextGeneration_++;
    try { publish(index); }
    catch (...) { slot.game.reset(); slot.snapshots.clear(); throw; }
    return {{uint64_t(index), slot.generation}, {1}};
}
PlayerBinding GameHost::admit(GameHandle handle, server::GameDefinition definition) {
    auto *slot = find(handle);
    if (!slot) throw std::runtime_error("Game instance expired");
    const auto id = slot->game->admit(std::move(definition));
    try { publish(size_t(handle.slot)); }
    catch (...) { slot->game->remove(id); throw; }
    return {handle, id};
}
bool GameHost::remove(PlayerBinding binding) {
    auto *slot = find(binding.game);
    if (!slot || !slot->game->remove(binding.player)) return false;
    publish(size_t(binding.game.slot)); return true;
}
bool GameHost::enter(PlayerBinding binding, bool active) {
    auto *slot = find(binding.game);
    if (!slot || !slot->game->enter(binding.player, active)) return false;
    publish(size_t(binding.game.slot)); return true;
}
std::vector<PlayerId> GameHost::participants(GameHandle handle) const {
    const auto *slot = find(handle); return slot ? slot->game->participants() : std::vector<PlayerId>{};
}
std::vector<PlayerId> GameHost::visiblePlayers(PlayerBinding binding) const {
    const auto *slot = find(binding.game); return slot ? slot->game->visiblePlayers(binding.player) : std::vector<PlayerId>{};
}
std::vector<RegionId> GameHost::visibleAreas(PlayerBinding binding) const {
    const auto *slot = find(binding.game); return slot ? slot->game->visibleAreas(binding.player) : std::vector<RegionId>{};
}
bool GameHost::npcVisible(PlayerBinding binding,std::string_view code,RegionId area,int initFunction) const {
    const auto *slot=find(binding.game);return slot && slot->game->npcVisible(binding.player,code,area,initFunction);
}
server::DomainResult<> GameHost::relocate(PlayerBinding binding,RegionId area,std::optional<Vec> position) {
    auto *slot=find(binding.game);if(!slot) return {server::DomainStatus::InvalidActor,{}};
    auto result=slot->game->relocate(binding.player,area,position);if(result) publish(size_t(binding.game.slot));return result;
}
std::optional<uint16_t> GameHost::npcQuestAlert(PlayerBinding binding,const server::NpcRule &npc,RegionId area) const {
    const auto *slot=find(binding.game);return slot?slot->game->npcQuestAlert(binding.player,npc,area):std::nullopt;
}
std::optional<server::AreaView> GameHost::area(GameHandle handle, RegionId id) const {
    const auto *slot = find(handle); const auto *area = slot ? slot->game->area(id) : nullptr;
    if (!area) return {};
    return server::AreaView{area->definition, area->generation, {}, 0};
}
std::optional<server::GameSettings> GameHost::settings(GameHandle handle) const {
    const auto *slot = find(handle); return slot ? std::optional{slot->game->settings()} : std::nullopt;
}
uint64_t GameHost::nextEntity(GameHandle handle) const {
    const auto *slot = find(handle); if (!slot) throw std::runtime_error("Game instance expired"); return slot->game->nextEntity();
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
        if (paused && !slot->debugPaused) slot->game->suspend();
        publish(size_t(handle.slot));
    }
    return true;
}
void GameHost::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return;
    for (size_t index = 0; index < slots_.size(); ++index) {
        auto &slot = slots_[index];
        if (!slot.game || slot.debugPaused.value_or(slot.paused)) continue;
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
    if (slot->paused && !slot->debugPaused) return CommandStatus::Paused;
    return slot->game->enqueue(binding.player, std::move(command));
}
bool GameHost::step(GameHandle handle, uint32_t frames) {
    auto *slot = find(handle);
    if (!slot || !slot->debugPaused.value_or(slot->paused) || !frames || frames > 250) return false;
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
        snapshot->game = {uint64_t(index), slot.generation}; snapshot->paused = slot.debugPaused.value_or(slot.paused);
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
void GameHost::failArea(GameHandle handle, uint64_t request) {
    if (auto *slot = find(handle)) slot->game->failArea(request);
}
server::DomainResult<> GameHost::grantExperience(PlayerBinding binding, uint64_t amount) {
    auto *slot = find(binding.game);
    if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->grantExperience(binding.player, amount);
    if (result) publish(size_t(binding.game.slot));
    return result;
}

std::set<int> GameHost::unitStates(GameHandle game,EntityId id) const {
    const auto *slot=find(game);return slot?slot->game->unitStates(id):std::set<int>{};
}
std::vector<MonsterSnapshot> GameHost::visibleMonsters(PlayerBinding binding) const {
    const auto *slot = find(binding.game);
    return slot ? slot->game->visibleMonsters(binding.player) : std::vector<MonsterSnapshot>{};
}
std::vector<PetOwnershipSnapshot> GameHost::pets(PlayerBinding binding) const {
    const auto *slot=find(binding.game);
    return slot ? slot->game->pets(binding.player) : std::vector<PetOwnershipSnapshot>{};
}

bool GameHost::debugPause(GameHandle handle, std::optional<bool> paused) {
    auto *slot = find(handle);
    if (!slot) return false;
    const bool wasPaused = slot->debugPaused.value_or(slot->paused);
    slot->debugPaused = paused; slot->accumulator = 0;
    slot->resumed = !slot->debugPaused.value_or(slot->paused);
    if (wasPaused != slot->debugPaused.value_or(slot->paused) && !paused && slot->paused) slot->game->suspend();
    publish(size_t(handle.slot));
    return true;
}
server::DomainResult<> GameHost::restoreResources(PlayerBinding binding) {
    auto *slot = find(binding.game);
    if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->restoreResources(binding.player);
    if (result) publish(size_t(binding.game.slot));
    return result;
}
server::DomainResult<EntityId> GameHost::spawnMonster(PlayerBinding binding, const server::PreparedMonster &monster) {
    auto *slot = find(binding.game);
    if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->spawnMonster(binding.player, monster);
    if (result) publish(size_t(binding.game.slot));
    return result;
}
std::optional<server::DiagnosticSnapshot> GameHost::diagnostics(PlayerBinding binding, size_t limit, uint64_t since,
    uint64_t commandSince, std::optional<Vec> destination) const {
    const auto *slot = find(binding.game);
    if (!slot) return {};
    auto result = slot->game->diagnostics(binding.player, limit, since, commandSince, destination);
    if (result) { result->player.game = binding.game; result->player.paused = slot->debugPaused.value_or(slot->paused); }
    return result;
}

} // namespace d2x

namespace d2x {
std::vector<ItemInstance> GameHost::groundItems(PlayerBinding binding) const { const auto *slot = find(binding.game); return slot ? slot->game->groundItems(binding.player) : std::vector<ItemInstance>{}; }
std::vector<server::CorpseView> GameHost::visibleCorpses(PlayerBinding binding) const { const auto *slot = find(binding.game); return slot ? slot->game->visibleCorpses(binding.player) : std::vector<server::CorpseView>{}; }
std::vector<server::objects::Object> GameHost::visibleObjects(PlayerBinding binding) const { const auto *slot = find(binding.game); return slot ? slot->game->visibleObjects(binding.player) : std::vector<server::objects::Object>{}; }
server::DomainResult<> GameHost::spawnItems(PlayerBinding binding, server::items::PreparedBatch batch, std::optional<Vec> position) {
    auto *slot = find(binding.game); if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->spawnItems(binding.player, std::move(batch), position); if (result) publish(size_t(binding.game.slot)); return result;
}
std::map<EntityId, server::loot::Preparation> GameHost::pendingLoot(GameHandle game) const { const auto *slot = find(game); return slot ? slot->game->pendingLoot() : std::map<EntityId, server::loot::Preparation>{}; }
server::DomainResult<> GameHost::installLoot(GameHandle game, EntityId source, server::items::PreparedBatch batch, std::string deferred) {
    auto *slot = find(game); if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->installLoot(source, std::move(batch), std::move(deferred)); if (result) publish(size_t(game.slot)); return result;
}
}

namespace d2x {
std::vector<server::merchant::Preparation> GameHost::pendingMerchant(GameHandle game) const { const auto *slot=find(game); return slot ? slot->game->pendingMerchant() : std::vector<server::merchant::Preparation>{}; }
std::vector<server::crafting::Preparation> GameHost::pendingCrafting(GameHandle game) const { const auto *slot=find(game); return slot ? slot->game->pendingCrafting() : std::vector<server::crafting::Preparation>{}; }
std::vector<server::quests::Preparation> GameHost::pendingQuests(GameHandle game) const {const auto *slot=find(game);return slot?slot->game->pendingQuests():std::vector<server::quests::Preparation>{};}
server::DomainResult<> GameHost::installQuests(GameHandle game,server::quests::Prepared prepared) {auto *slot=find(game);if(!slot) return {server::DomainStatus::Stale,{}};auto result=slot->game->installQuests(std::move(prepared));if(result) publish(game.slot);return result;}
server::DomainResult<> GameHost::installCrafting(GameHandle game, server::crafting::Prepared prepared) { auto *slot=find(game); if(!slot) return {server::DomainStatus::Stale,{}}; auto result=slot->game->installCrafting(std::move(prepared)); if(result) publish(game.slot); return result; }
server::DomainResult<> GameHost::installMerchant(GameHandle game,server::merchant::Prepared prepared) { auto *slot=find(game); if(!slot) return {server::DomainStatus::Stale,{}}; auto result=slot->game->installMerchant(std::move(prepared)); if(result) publish(size_t(game.slot)); return result; }
std::optional<PersistentCharacter> GameHost::shop(PlayerBinding binding) const { const auto *slot=find(binding.game); return slot ? slot->game->shop(binding.player) : std::nullopt; }
}

namespace d2x {
server::DomainResult<> GameHost::grantGold(PlayerBinding binding,uint32_t amount) { auto *slot=find(binding.game); if(!slot) return {server::DomainStatus::InvalidActor,{}}; auto result=slot->game->grantGold(binding.player,amount); if(result) publish(size_t(binding.game.slot)); return result; }
server::DomainResult<> GameHost::missileHit(PlayerBinding binding,EntityId source,uint32_t amount,DamageType type,bool returnFire) {
    auto *slot=find(binding.game);if(!slot) return {server::DomainStatus::InvalidActor,{}};
    auto result=slot->game->missileHit(binding.player,source,amount,type,returnFire);if(result) publish(size_t(binding.game.slot));return result;
}
server::DomainResult<> GameHost::damagePlayer(PlayerBinding binding,uint32_t amount) { auto *slot=find(binding.game); if(!slot) return {server::DomainStatus::InvalidActor,{}}; auto result=slot->game->damagePlayer(binding.player,amount); if(result) publish(size_t(binding.game.slot)); return result; }
}

namespace d2x {
std::vector<server::travel::Portal> GameHost::visiblePortals(PlayerBinding binding) const { const auto *slot=find(binding.game); return slot?slot->game->visiblePortals(binding.player):std::vector<server::travel::Portal>{}; }
}

namespace d2x {
server::DomainResult<> GameHost::damageMonster(PlayerBinding binding, EntityId target, std::optional<uint32_t> amount) {
    auto *slot = find(binding.game);
    if (!slot) return {server::DomainStatus::InvalidActor, {}};
    auto result = slot->game->damageMonster(binding.player, target, amount);
    if (result) publish(size_t(binding.game.slot));
    return result;
}
}

namespace d2x {
std::vector<server::companions::Preparation> GameHost::pendingSummons(GameHandle game) const {const auto *slot=find(game);return slot?slot->game->pendingSummons():std::vector<server::companions::Preparation>{};}
server::DomainResult<> GameHost::installSummon(GameHandle game,server::companions::Prepared prepared) {auto *slot=find(game);return slot?slot->game->installSummon(std::move(prepared)):server::DomainResult<>{server::DomainStatus::Stale,{}};}
std::vector<server::companions::HirelingPreparation> GameHost::pendingHirelings(GameHandle game) const {const auto *slot=find(game);return slot?slot->game->pendingHirelings():std::vector<server::companions::HirelingPreparation>{};}
std::vector<server::companions::HirelingListPreparation> GameHost::pendingHirelingLists(GameHandle game) const {const auto *slot=find(game);return slot?slot->game->pendingHirelingLists():std::vector<server::companions::HirelingListPreparation>{};}
server::DomainResult<> GameHost::installHirelingList(GameHandle game,server::companions::PreparedHirelingList prepared) {auto *slot=find(game);return slot?slot->game->installHirelingList(std::move(prepared)):server::DomainResult<>{server::DomainStatus::Stale,{}};}
server::DomainResult<> GameHost::installHireling(GameHandle game,server::companions::PreparedHireling prepared) {auto *slot=find(game);return slot?slot->game->installHireling(std::move(prepared)):server::DomainResult<>{server::DomainStatus::Stale,{}};}
}
