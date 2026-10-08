#pragma once
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/game_command.hpp"
#include "server/runtime/game_systems.hpp"
#include "server/runtime/simulation.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/diagnostics.hpp"
#include <optional>

namespace d2x::server {
struct GameDefinition {
    GameSettings settings;
    AreaDefinition area;
    CharacterDefinition character;
    PersistentCharacter persistent;
    uint64_t rulesFingerprint{};
    PreparedRules rules;
};
struct CorpseView { PlayerCorpse corpse; PersistentCharacter equipment; uint64_t revision{}; };
// Composition and ordering only; domain implementations live beside this class.
class GameInstance {
    const GameSettings settings_;
    AreaStore areas_;
    EntityIds entities_;
    uint64_t random_, tick_{}, revision_{1};
    uint64_t rulesFingerprint_{};
    uint64_t nextPlayer_ = 2;
    const PreparedRules rules_;
    PlayerStore players_;
    EventOutbox events_;
    GameSystems systems_;
    Simulation simulation_;
    struct Pending { PlayerId player; GameCommand command; };
    std::deque<Pending> commands_;
    std::array<DiagnosticCommand, 512> commandHistory_{};
    uint64_t commandHistoryNext_ = 1;
    void observeCommand(PlayerId, uint64_t sequence, CommandStatus) noexcept;
  public:
    explicit GameInstance(GameDefinition);
    GameInstance(const GameInstance &) = delete;
    GameInstance &operator=(const GameInstance &) = delete;
    CommandStatus enqueue(PlayerId, GameCommand);
    void step();
    void suspend();
    PlayerId admit(GameDefinition);
    bool remove(PlayerId);
    bool enter(PlayerId, bool active = true);
    uint64_t nextEntity() const { return entities_.cursor(); }
    const GameSettings &settings() const { return settings_; }
    const AreaState *area(RegionId id) const { return areas_.find(id); }
    std::vector<RegionId> visibleAreas(PlayerId) const;
    std::vector<PlayerId> visiblePlayers(PlayerId) const;
    std::vector<MonsterSnapshot> visibleMonsters(PlayerId) const;
    std::set<int> unitStates(EntityId id) const {return systems_.effects.unitStates(id,tick_); }
    std::optional<PlayerSnapshot> snapshot(PlayerId) const;
    std::optional<PersistentCharacter> exportCharacter(PlayerId) const;
    std::optional<PersistentCharacter> publicEquipment(PlayerId) const;
    std::optional<inventory::InputState> inventoryInput(PlayerId id) const { return systems_.inventory.input(id); }
    std::vector<PlayerId> participants() const;
    std::vector<ItemInstance> groundItems(PlayerId) const;
    std::vector<CorpseView> visibleCorpses(PlayerId) const;
    std::vector<objects::Object> visibleObjects(PlayerId) const;
    std::vector<travel::Portal> visiblePortals(PlayerId) const;
    DomainResult<> spawnItems(PlayerId, items::PreparedBatch, std::optional<Vec>);
    auto pendingMerchant() const { return systems_.merchant.pending(); }
    DomainResult<> installMerchant(merchant::Prepared prepared) { return systems_.merchant.install(std::move(prepared)); }
    auto shop(PlayerId id) const { return systems_.merchant.shop(id); }
    const auto &pendingLoot() const { return systems_.loot.read().pending; }
    DomainResult<> installLoot(EntityId source, items::PreparedBatch batch, std::string deferred) { return systems_.loot.install(source, std::move(batch), std::move(deferred)); }
    const SystemSteps &systemSteps() const { return simulation_.lastSteps(); }
    const std::deque<EventBatch> &pendingEvents() const { return events_.pending(); }
    bool acknowledgeEvents(uint64_t sequence) { return events_.acknowledge(sequence); }
    const std::vector<world::PrepareArea> &pendingAreas() const { return systems_.world.read().preparation; }
    DomainResult<> installArea(world::PreparedArea);
    void failArea(uint64_t request) { systems_.world.fail(request); }
    DomainResult<> grantExperience(PlayerId, uint64_t amount);
    DomainResult<> restoreResources(PlayerId);
    DomainResult<> grantGold(PlayerId,uint32_t);
    DomainResult<> damagePlayer(PlayerId,uint32_t);
    DomainResult<> missileHit(PlayerId,EntityId,uint32_t,DamageType,bool returnFire);
    DomainResult<> damageMonster(PlayerId,EntityId,std::optional<uint32_t> amount);
    DomainResult<EntityId> spawnMonster(PlayerId, const PreparedMonster &);
    std::optional<DiagnosticSnapshot> diagnostics(PlayerId, size_t limit, uint64_t since, uint64_t commandSince,
                                                std::optional<Vec> destination = {}) const;
};
} // namespace d2x::server
