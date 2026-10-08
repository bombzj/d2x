#pragma once
#include "server/game_instance.hpp"
#include <memory>
#include <vector>

namespace d2x {
// No window, network, archive or storage dependencies. Call from one host
// scheduler thread; a future transport posts commands onto that thread.
class GameHost {
    struct Slot {
        uint64_t generation{};
        std::unique_ptr<server::GameInstance> game;
        double accumulator{};
        bool paused{};
        std::optional<bool> debugPaused;
        bool resumed{};
        std::map<PlayerId, std::shared_ptr<const PlayerSnapshot>> snapshots;
    };
    std::vector<Slot> slots_;
    uint64_t nextGeneration_ = 1;
    Slot *find(GameHandle);
    const Slot *find(GameHandle) const;
    void publish(size_t slot);
  public:
    PlayerBinding create(server::GameDefinition);
    bool destroy(GameHandle);
    PlayerBinding admit(GameHandle, server::GameDefinition);
    bool remove(PlayerBinding);
    bool enter(PlayerBinding, bool active = true);
    std::vector<PlayerId> participants(GameHandle) const;
    std::vector<PlayerId> visiblePlayers(PlayerBinding) const;
    std::vector<MonsterSnapshot> visibleMonsters(PlayerBinding) const;
    std::vector<PetOwnershipSnapshot> pets(PlayerBinding) const;
    std::set<int> unitStates(GameHandle,EntityId) const;
    std::vector<server::companions::Preparation> pendingSummons(GameHandle) const;
    server::DomainResult<> installSummon(GameHandle,server::companions::Prepared);
    std::vector<server::merchant::Preparation> pendingMerchant(GameHandle) const;
    server::DomainResult<> installMerchant(GameHandle, server::merchant::Prepared);
    std::optional<PersistentCharacter> shop(PlayerBinding) const;
    std::vector<ItemInstance> groundItems(PlayerBinding) const;
    std::vector<server::CorpseView> visibleCorpses(PlayerBinding) const;
    std::vector<server::objects::Object> visibleObjects(PlayerBinding) const;
    std::vector<server::travel::Portal> visiblePortals(PlayerBinding) const;
    server::DomainResult<> spawnItems(PlayerBinding, server::items::PreparedBatch, std::optional<Vec>);
    std::map<EntityId, server::loot::Preparation> pendingLoot(GameHandle) const;
    server::DomainResult<> installLoot(GameHandle, EntityId, server::items::PreparedBatch, std::string);
    std::vector<RegionId> visibleAreas(PlayerBinding) const;
    std::optional<server::AreaView> area(GameHandle, RegionId) const;
    std::optional<server::GameSettings> settings(GameHandle) const;
    uint64_t nextEntity(GameHandle) const;
    bool pause(GameHandle, bool);
    bool debugPause(GameHandle, std::optional<bool>);
    void advance(double seconds);
    // Explicit authority stepping while paused; no accumulated wall-clock debt.
    bool step(GameHandle, uint32_t frames);
    CommandStatus submit(PlayerBinding, server::GameCommand);
    std::shared_ptr<const PlayerSnapshot> read(PlayerBinding) const;
    // Host storage boundary only; never exposed on a client transport.
    std::optional<PersistentCharacter> exportCharacter(PlayerBinding) const;
    std::optional<PersistentCharacter> publicEquipment(PlayerBinding) const;
    std::optional<server::inventory::InputState> inventoryInput(PlayerBinding) const;
    std::optional<server::SystemSteps> systemSteps(GameHandle) const;
    // Reliable game facts stay on the host side; native encoding must complete
    // before acknowledging. A failed encoder cannot silently discard output.
    std::optional<std::vector<server::EventBatch>> pendingEvents(GameHandle) const;
    bool acknowledgeEvents(GameHandle, uint64_t sequence);
    server::DomainResult<> grantExperience(PlayerBinding, uint64_t amount);
    server::DomainResult<> restoreResources(PlayerBinding);
    server::DomainResult<> grantGold(PlayerBinding,uint32_t);
    server::DomainResult<> damagePlayer(PlayerBinding,uint32_t);
    server::DomainResult<> missileHit(PlayerBinding,EntityId,uint32_t,DamageType,bool returnFire);
    server::DomainResult<> damageMonster(PlayerBinding,EntityId,std::optional<uint32_t> amount);
    server::DomainResult<EntityId> spawnMonster(PlayerBinding, const server::PreparedMonster &);
    std::optional<server::DiagnosticSnapshot> diagnostics(PlayerBinding, size_t limit, uint64_t since, uint64_t commandSince,
                                                         std::optional<Vec> destination = {}) const;
    // Content worker results are installed on the scheduler thread. The worker
    // may prepare values but must not retain a mutable game/area reference.
    std::optional<std::vector<server::world::PrepareArea>> pendingAreas(GameHandle) const;
    server::DomainResult<> installArea(GameHandle, server::world::PreparedArea);
    void failArea(GameHandle, uint64_t request);
};
} // namespace d2x
