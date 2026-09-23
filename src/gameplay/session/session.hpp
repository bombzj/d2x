#pragma once
#include "content/classic_data.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/model/interaction.hpp"
#include "gameplay/session/session_snapshot.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "world/population.hpp"
#include "world/region.hpp"

namespace d2x {
class GameSession {
    EntityIds ids_;
    ClassicData content_;
    WorldCatalog worldContent_;
    MonsterCatalog monsterContent_;
    std::vector<WorldEntry> worldEntries_;
    uint64_t contentFingerprint_ = 0;
    Simulation simulation_{ids_};
    InventoryService inventory_{ids_, content_.items};
    LootSystem loot_;
    ItemHandle pickup_{};
    PlayerContainers playerContainers_;
    StorageAccess storage_;
    EntityId pendingInteraction_;
    bool pendingInteractionRepath_ = false;
    EntityId engagedNpc_;
    std::optional<uint64_t> pendingPortal_;
    std::optional<Vec> townPortalArrival_;
    float portalReach_ = 0;
    bool portalResources_ = false;
    std::optional<int> pendingExit_;
    std::optional<Vec> boundaryMoveTarget_;
    std::vector<Region> regions_;
    std::vector<AreaState> inactiveAreas_;
    std::vector<GameCommand> pending_;
    int current_ = -1;
    void enter(RegionId id, std::optional<Vec> arrival = {});
    void beginExit(int slot);
    bool routeBoundaryMove(Vec target);
    void updateExit();
    void cancelExit();
    PopulationPlan population(const Region &region) const;
    void interact(EntityId object);
    void updateInteraction();
    void cancelInteraction();
    void completeInteraction(const WorldObject &object);
    void identifyWithCain(EntityId npc);
    void advanceNpcPaths(float dt);
    bool canReach(const WorldObject &object) const;
    std::optional<Vec> interactionApproach(const WorldObject &object) const;
    bool travelWaypoint(const WaypointTravel &command);
    void validateStorage();
    void closeStorage();
    void spawnLoot(std::span<const LootDrop> drops, RegionId region, Vec origin);
    InventoryAccess inventoryAccess() const;
    EquipmentActor equipmentActor() const;
    void createStarterEquipment();
    bool inventoryDestinationAllowed(const ItemDestination &destination) const;
    bool inventorySourceAllowed(EntityId item) const;
    void publishInventory(InventoryResult result, EntityId requested);
    void executeInventory(const GameCommand &command);
    void settleDeaths();
    void useItem(ItemHandle item);
    void beginPortal(uint64_t revision);
    void updatePortal();
    InventoryError previewPortalScroll(ItemHandle item) const;
    void useBeltColumn(int column);
    void beginPickup(ItemHandle item);
    void updatePickup();
    void cancelPickup();
    int validateSnapshot(const SessionSnapshot &snapshot) const;
    void validateItemProperties(const SessionSnapshot &snapshot) const;

  public:
    static constexpr float fixedStep = 1.f / 25.f;
    GameSession(Archives &archives, const WorldSelection &selection, int startRegion = -1,
                uint64_t lootSeed = LootSystem::defaultSeed, PopulationSettings population = {});
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;
    const WorldState &state() const { return simulation_.state(); }
    bool active(Vec position) const { return simulation_.active(position); }
    const ClassicData &content() const { return content_; }
    const WorldCatalog &worldContent() const { return worldContent_; }
    const MonsterCatalog &monsterContent() const { return monsterContent_; }
    const auto &worldEntries() const { return worldEntries_; }
    uint64_t contentFingerprint() const { return contentFingerprint_; }
    SessionSnapshot snapshot() const;
    // Validate completely before replacing live state; a rejected load changes nothing.
    void restore(SessionSnapshot snapshot);
    const InventoryService &inventory() const { return inventory_; }
    const EquipmentStats &equipmentStats() const { return simulation_.equipmentStats_; }
    const PlayerContainers &playerContainers() const { return playerContainers_; }
    StorageAccess storage() const;
    const WorldObject *object(EntityId id) const;
    EntityId interactionTarget() const { return pendingInteraction_; }
    EntityId pickupTarget() const { return pickup_.id; }
    std::optional<Vec> portalPosition() const;
    bool waypointUnlocked(RegionId region) const { return state().waypoints.contains(region); }
    InventoryError previewInventory(const GameCommand &command) const;
    std::optional<GroundLocation> dropLocation() const;
    const Map &map() const { return regions_.at(current_).map; }
    const Region &region() const { return regions_.at(current_); }
    const std::vector<Region> &regions() const { return regions_; }
    int regionIndex() const { return current_; }
    std::span<const GameEvent> events() const { return simulation_.events(); }
    void submit(GameCommand command) { pending_.push_back(std::move(command)); }
    void tick(float dt, Vec keyboard = {});
};
} // namespace d2x
