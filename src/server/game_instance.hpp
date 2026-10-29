#pragma once
#include "server/game_messages.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/game_command.hpp"
#include "server/runtime/game_systems.hpp"
#include "server/runtime/simulation.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/diagnostics.hpp"
#include "core/native_unit_id.hpp"
#include <optional>

namespace d2x { enum class PointRefund; }
namespace d2x::server {
struct GameDefinition {
    GameSettings settings;
    AreaDefinition area;
    CharacterDefinition character;
    PersistentCharacter persistent;
    PreparedRules rules;
};
struct CorpseView { PlayerCorpse corpse; PersistentCharacter equipment; uint64_t revision{}; };
// Composition and ordering only; domain implementations live beside this class.
class GameInstance {
    GameSettings settings_;
    uint32_t objectSeed_{}; // Original dwObjSeed: the control stream's initial low word.
    AreaStore areas_;
    NativeUnitIds nativeEntities_;
    EntityIds entities_;
    uint64_t random_, tick_{}, revision_{1};
    uint64_t nextPlayer_ = 2;
    const PreparedRules rules_;
    PlayerStore players_;
    EventOutbox events_;
    GameSystems systems_;
    Simulation simulation_;
    bool simulationPending_{};
    struct Pending {
        PlayerId player; GameCommand command;
        bool awaitingCrafting{};
        CommandStatus dispatchedResult{CommandStatus::Unavailable};
    };
    std::deque<Pending> commands_;
    bool pendingSynchronousCommand(PlayerId,uint64_t sequence=0) const;
    std::array<DiagnosticCommand, 512> commandHistory_{};
    uint64_t commandHistoryNext_ = 1;
    void observeCommand(PlayerId, uint64_t sequence, CommandStatus) noexcept;
  public:
    explicit GameInstance(GameDefinition);
    GameInstance(const GameInstance &) = delete;
    GameInstance &operator=(const GameInstance &) = delete;
    CommandStatus enqueue(PlayerId, GameCommand);
    void step();
    uint64_t tick() const noexcept {return tick_;}
    void suspend();
    PlayerId admit(GameDefinition);
    bool remove(PlayerId);
    bool enter(PlayerId, bool active = true);
    uint64_t nextEntity() const { return entities_.cursor(); }
    std::optional<NativeUnitId> nativeIdentity(EntityId id) const {return nativeEntities_.find(id);}
    std::optional<EntityId> entityIdentity(NativeUnitId id) const {return nativeEntities_.find(id);}
    void collectNativeIdentities(std::set<EntityId> retained);
    const GameSettings &settings() const { return settings_; }
    uint32_t objectSeed() const { return objectSeed_; }
    const AreaState *area(RegionId id) const { return areas_.find(id); }
    std::optional<NativeActEnvironment> environment(RegionId id) const {return areas_.environment(id);}
    std::vector<RegionId> visibleAreas(PlayerId) const;
    std::vector<RoomView> visibleRooms(PlayerId id) const { return systems_.world.visibleRooms(id); }
    bool clientLoadReady(PlayerId) const;
    bool visiblePoint(PlayerId id, RegionId region, Vec point) const { return systems_.world.visible(id, region, point); }
    bool npcVisible(PlayerId player,std::string_view code,RegionId area,int initFunction=0) const {
        const auto *p=players_.find(player);return p && systems_.quests.npcVisible(p->persistent.player,code,area,initFunction);
    }
    std::optional<uint16_t> npcQuestAlert(PlayerId player,const NpcRule &npc,RegionId area) const {
        const auto *p=players_.find(player);const auto *a=areas_.find(area);
        if(!p || !a || !systems_.quests.npcVisible(p->persistent.player,npc.code,area,npc.questInitFunction)) return {};
        const auto message=systems_.quests.dialogue({player,p->actor,area,a->generation,0,tick_},npc);
        return message && message->message.menu!=2?std::optional<uint16_t>{message->message.text}:std::nullopt;
    }
    std::vector<PlayerId> visiblePlayers(PlayerId) const;
    std::vector<MonsterSnapshot> visibleMonsters(PlayerId) const;
    std::vector<MissileFact> visibleMissiles(PlayerId id) const {return systems_.missiles.visible(id);}
    std::vector<PetOwnershipSnapshot> pets(PlayerId id) const {return systems_.replication.pets(id);}
    std::set<int> unitStates(EntityId id) const {return systems_.effects.unitStates(id,tick_); }
    std::optional<PlayerSnapshot> snapshot(PlayerId) const;
    bool prepareSave(PlayerId);
    std::optional<PersistentCharacter> exportCharacter(PlayerId,bool settledOnly=true) const;
    std::optional<PersistentCharacter> publicEquipment(PlayerId) const;
    std::optional<inventory::InputState> inventoryInput(PlayerId id) const { return systems_.inventory.input(id); }
    std::vector<PlayerId> participants() const;
    std::vector<ItemInstance> groundItems(PlayerId) const;
    std::vector<CorpseView> visibleCorpses(PlayerId) const;
    std::optional<QuestFact> questProjection(PlayerId id) const { const auto *p=players_.find(id);return p?std::optional<QuestFact>{systems_.quests.projection(id,p->persistent.player,p->area)}:std::nullopt; }
    std::map<EntityId, EntityId> corpseOwners() const;
    std::vector<objects::Object> visibleObjects(PlayerId) const;
    std::vector<travel::Portal> visiblePortals(PlayerId) const;
    DomainResult<> spawnItems(PlayerId, items::PreparedBatch, std::optional<Vec>);
    auto pendingSummons() const {return systems_.companions.pending();}
    auto pendingHirelings() const {return systems_.companions.pendingHirelings(tick_);}
    auto pendingHirelingLists() const {return systems_.companions.pendingHirelingLists();}
    DomainResult<> installHirelingList(companions::PreparedHirelingList prepared) {return systems_.companions.install(std::move(prepared));}
    DomainResult<> installHireling(companions::PreparedHireling prepared) {return systems_.companions.install(std::move(prepared));}
    DomainResult<> installSummon(companions::Prepared prepared) {return systems_.companions.install(std::move(prepared));}
    auto pendingMerchant() const { return systems_.merchant.pending(); }
    auto pendingCrafting() const { return systems_.crafting.pending(); }
    auto pendingQuests() const { return systems_.quests.pending(); }
    DomainResult<> installQuests(quests::Prepared prepared) { return systems_.quests.install(std::move(prepared)); }
    DomainResult<> installCrafting(crafting::Prepared prepared) { return systems_.crafting.install(std::move(prepared)); }
    DomainResult<> installMerchant(merchant::Prepared prepared) { return systems_.merchant.install(std::move(prepared)); }
    auto shop(PlayerId id) const { return systems_.merchant.shop(id); }
    auto pendingLoot() const { auto pending=systems_.loot.read().pending; for(auto &[id,value]:pending) { (void)id; value.uniques=systems_.loot.read().uniques; } return pending; }
    auto pendingGroundGeneration() const {return systems_.objects.pendingGroundGeneration();}
    auto pendingInactiveItem() {return systems_.items.pendingInactiveItem();}
    DomainResult<> prepareInactiveItem(items::PreparedInactiveItem prepared) {return systems_.items.prepareInactiveItem(std::move(prepared));}
    auto pendingModifiers() const {return systems_.monsters.pendingModifiers();}
    auto pendingMonsterEquipment() {return systems_.monsters.pendingEquipment();}
    DomainResult<> prepareMonsterEquipment(monsters::PreparedEquipment prepared) {return systems_.monsters.prepareEquipment(std::move(prepared));}
    DomainResult<> prepareModifiers(monsters::PreparedModifiers prepared) {return systems_.monsters.prepareModifiers(std::move(prepared));}
    DomainResult<> prepareGroundGeneration(uint64_t request,items::PreparedBatch batch,std::string deferred) {
        return systems_.objects.prepareGroundGeneration(request,std::move(batch),std::move(deferred));
    }
    const auto &usedUniques() const {return systems_.loot.read().uniques;}
    auto reserveLootSeeds(EntityId source,uint64_t occurrence,size_t index) {return systems_.loot.reserveSeeds(source,occurrence,index);}
    auto beginLootPreparation(EntityId source) {return systems_.loot.beginPreparation(source);}
    DomainResult<> installLoot(EntityId source, items::PreparedBatch batch, std::string deferred,std::optional<uint64_t> random={},std::optional<uint64_t> objectRandom={},std::optional<uint32_t> pd2Random={}) { return systems_.loot.install(source, {tick_}, std::move(batch), std::move(deferred),random,objectRandom,pd2Random); }
    const SystemSteps &systemSteps() const { return simulation_.lastSteps(); }
    const SystemMetrics &stageMetrics() const { return simulation_.metrics(); }
    void profileFootprints(bool enabled) { systems_.spatial.profileFootprints(enabled); }
    const spatial::FootprintMetrics &footprintMetrics() const { return systems_.spatial.footprintMetrics(); }
    const std::deque<EventBatch> &pendingEvents() const { return events_.pending(); }
    bool acknowledgeEvents(uint64_t sequence) { return events_.acknowledge(sequence); }
    const std::vector<world::PrepareArea> &pendingAreas() const { return systems_.world.read().preparation; }
    DomainResult<> installArea(world::PreparedArea);
    void failArea(uint64_t request) { systems_.world.fail(request); }
    DomainResult<> grantExperience(PlayerId, uint64_t amount);
    DomainResult<> grantHireling(PlayerId,HirelingRecord);
    DomainResult<> restoreResources(PlayerId);
    DomainResult<> grantGold(PlayerId,uint32_t);
    DomainResult<> setVirtualPlayers(PlayerId,unsigned);
    DomainResult<> damagePlayer(PlayerId,uint32_t);
    DomainResult<> missileHit(PlayerId,EntityId,uint32_t,DamageType,bool returnFire);
    DomainResult<> damageMonster(PlayerId,EntityId,std::optional<uint32_t> amount);
    DomainResult<> relocate(PlayerId,RegionId,std::optional<Vec>);
    DomainResult<> unlockWaypoints(PlayerId,std::optional<RegionId>);
    DomainResult<> resetPoints(PlayerId,PointRefund);
    DomainResult<EntityId> spawnMonster(PlayerId, const PreparedMonster &);
    DomainResult<EntityId> spawnObject(PlayerId,const objects::Admission &);
    std::optional<DiagnosticSnapshot> diagnostics(PlayerId, size_t limit, uint64_t since, uint64_t commandSince,
                                                std::optional<Vec> destination = {}) const;
};
} // namespace d2x::server
