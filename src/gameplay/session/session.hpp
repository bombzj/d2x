#pragma once
#include "content/classic_data.hpp"
#include "content/monster_difficulty_combat.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/model/interaction.hpp"
#include "gameplay/npc/store.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "gameplay/session/character_save.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "world/population.hpp"
#include "world/region.hpp"

namespace d2x {
struct ShrineStatus {
    std::string name, effect;
    float until = 0;
};
struct NpcQuestDialogue {
    const NpcSpeech *speech = nullptr;
    std::optional<ActOneQuest> advancesQuest;
    bool automatic = false;
    std::string readKey;
};
struct HirelingCombatStats {
    HirelingStats base;
    int fireResist = 0, coldResist = 0, lightningResist = 0, poisonResist = 0;
    WeaponDamage weapon;
    CombatModifiers combat;
};
class GameSession {
    struct NpcMotionState {
        EntityId id;
        Vec position, look;
        std::deque<Vec> route;
        float wait = 0;
        int target = -1;
        uint64_t random = 0;
    };
    EntityIds ids_;
    ClassicData content_;
    CharacterDefinition characterDefinition_;
    WorldCatalog worldContent_;
    MonsterCatalog monsterContent_;
    mutable std::map<std::pair<std::string, RegionId>, std::optional<MonsterCombatProfile>> monsterCombatCache_;
    std::vector<WorldEntry> worldEntries_;
    uint64_t contentFingerprint_ = 0;
    std::optional<RegionId> denRegion_;
    std::optional<RegionId> burialRegion_;
    std::optional<RegionId> stonyRegion_, darkWoodRegion_, tristramRegion_;
    std::optional<RegionId> towerRegion_, towerCellarRegion_;
    std::optional<RegionId> barracksRegion_;
    std::optional<RegionId> catacombsFourRegion_;
    Simulation simulation_{ids_};
    InventoryService inventory_{ids_, content_.items, {content_.stashLayout.columns,
                                                       content_.stashLayout.rows},
                                     {content_.cubeLayout.columns, content_.cubeLayout.rows}};
    LootSystem loot_;
    ItemHandle pickup_{};
    PlayerContainers playerContainers_;
    StorageAccess storage_;
    EntityId pendingInteraction_;
    bool pendingInteractionRepath_ = false;
    EntityId engagedNpc_;
    // D2MOO quest GUID reaction lists are local to the current game.
    std::set<std::string> pendingNpcQuestMessages_;
    std::map<EntityId, std::vector<VendorOffer>> vendorStocks_;
    std::map<EntityId, std::set<uint32_t>> soldVendorOffers_;
    std::map<EntityId, std::vector<VendorOffer>> gambleStocks_;
    std::map<EntityId, std::vector<HirelingOffer>> hirelingOffers_;
    std::optional<uint64_t> pendingPortal_;
    bool pendingCainPortal_ = false;
    std::optional<Vec> townPortalArrival_;
    float portalReach_ = 0;
    float cainPortalReach_ = 0;
    bool portalResources_ = false;
    std::optional<int> pendingExit_;
    std::optional<Vec> boundaryMoveTarget_;
    std::optional<LevelExit::BoundaryPassage> boundaryPassage_;
    std::vector<Region> regions_;
    std::vector<NpcMotionState> initialNpcMotions_;
    std::vector<AreaState> inactiveAreas_;
    std::vector<ShrineStatus> shrineStatuses_;
    std::vector<GameCommand> pending_;
    int current_ = -1;
    void enter(RegionId id, std::optional<Vec> arrival = {}, std::optional<Vec> coordinateOffset = {});
    void beginExit(int slot);
    bool beginBoundaryExit(const LevelExit &exit, std::optional<Vec> target);
    bool routeBoundaryMove(Vec target);
    void updateExit();
    void cancelExit();
    PopulationPlan population(const Region &region) const;
    void interact(EntityId object);
    void updateInteraction();
    void cancelInteraction();
    void completeInteraction(const WorldObject &object);
    void activateLootObject(EntityId object);
    void activateShrine(EntityId object);
    void grantShrine(int code);
    void applyShrine(int code, std::string name, std::string effect, float duration);
    void drinkWell(EntityId object);
    void updateObjectTimers();
    void identifyWithCain(EntityId npc);
    void buyVendorItem(EntityId npc, uint32_t slot, bool gamble = false);
    void sellVendorItem(const SellVendorItem &command);
    void openGamble(EntityId npc);
    void repairVendorItem(const RepairVendorItem &command);
    std::vector<int> vendorQuestFactors(const VendorDefinition &vendor, bool repair, bool sale = false) const;
    void advanceNpcPaths(float dt);
    bool canReach(const WorldObject &object) const;
    std::optional<Vec> interactionApproach(const WorldObject &object) const;
    bool travelWaypoint(const WaypointTravel &command);
    void unlockWaypoints();
    void validateStorage();
    void closeStorage();
    void spawnLoot(std::span<const LootDrop> drops, RegionId region, Vec origin);
    void spawnDebugItem(const DebugSpawnItem &command);
    InventoryAccess inventoryAccess() const;
    EquipmentActor equipmentActor() const;
    EquipmentActor equipmentActor(const PlayerState &player) const;
    const CharacterDefinition &definitionFor(std::string_view name) const;
    void refreshCharacter(bool fillGains = false);
    void applyWarmth(CharacterAttributes &stats, const PlayerState &player,
                     const CharacterDefinition &definition, const InventoryService &inventory,
                     const PlayerContainers &containers, const EquipmentActor &actor) const;
    void expireCombatEffects();
    void grantExperience(uint64_t amount);
    void createStarterEquipment();
    bool inventoryDestinationAllowed(const ItemDestination &destination) const;
    bool inventorySourceAllowed(EntityId item) const;
    void publishInventory(InventoryResult result, EntityId requested);
    void executeInventory(const GameCommand &command);
    void settleDeaths();
    void onQuestRegionEntered(RegionId id);
    void updateDenQuest();
    void updateBurialQuest(const EnemyDied &death);
    void updateTowerQuest(const EnemyDied &death);
    void updateSlaughterQuest(const EnemyDied &death);
    void completeActOne(EntityId npc);
    void activateCainQuestObject(const WorldObject &object);
    void updateCainQuestItems();
    void updateToolsQuestItems();
    void activateMalus(const WorldObject &object);
    void imbueWithCharsi(const ImbueItem &command);
    void reconcileCainObjects();
    std::array<int, 5> cainStoneOrder() const;
    bool claimCainReward();
    void translateCainScroll(EntityId npc);
    bool travelCainPortal();
    bool beginCainPortal();
    void updateCainPortal();
    void advanceHireling(float dt);
    bool assignKashyaHireling();
    void openHirelingList(EntityId npc);
    void hireMercenary(const HireMercenary &command);
    void equipHirelingItem(const EquipHirelingItem &command);
    InventoryError previewHirelingEquipment(const EquipHirelingItem &command) const;
    bool ensureHirelingOffers(EntityId npc);
    void assignHireling(const HirelingOffer &offer);
    void grantDebugHireling();
    void grantHirelingExperience(const EnemyDied &death);
    HirelingCombatStats hirelingStats(const HirelingState &hireling, const InventoryService &inventory,
                                      const PlayerContainers &containers) const;
    void talkToNpc(EntityId npc);
    void claimAkaraRespec(EntityId npc);
    void useItem(ItemHandle item);
    void identifyItem(const IdentifyItem &command);
    void transactGold(const GoldTransaction &command);
    void dropDebugCube();
    void beginPortal(uint64_t revision);
    void updatePortal();
    InventoryError previewPortalScroll(ItemHandle item) const;
    void useBeltColumn(int column);
    void beginPickup(ItemHandle item);
    void updatePickup();
    void cancelPickup();
    int validateCharacterRestore(const CharacterSaveData &data) const;
    CharacterSaveData prepareCharacterRestore(CharacterSaveData character) const;
    void validateItemProperties(const CharacterSaveData &snapshot) const;
    void spawnDebugMonster(const DebugSpawnMonster &command);
    void damageDebugMonster(const DebugDamageMonster &command);
    std::optional<MonsterCombatProfile> resolvedMonsterCombat(
        const MonsterIdentity &identity, RegionId region) const;

  public:
    bool canHireFrom(EntityId npc) const;
    const std::vector<HirelingOffer> *hirelingOffers(EntityId npc) const;
    const HirelingDefinition *hirelingDefinition() const;
    HirelingCombatStats hirelingStats() const;
    static constexpr float fixedStep = 1.f / 25.f;
    GameSession(Archives &archives, const WorldSelection &selection, int startRegion = -1,
                uint64_t lootSeed = LootSystem::defaultSeed, PopulationSettings population = {},
                std::string characterClass = "Barbarian", std::string characterName = "Hero");
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;
    const WorldState &state() const { return simulation_.state(); }
    void setRunning(bool running) { simulation_.state_.player.running = running; }
    const QuestRecord &quest(ActOneQuest id, int difficulty) const {
        return state().player.actOneQuests.at(size_t(difficulty)).at(questIndex(id));
    }
    const QuestRecord &quest(ActOneQuest id) const { return quest(id, state().population.difficulty); }
    NpcQuestDialogue npcQuestDialogue(std::string_view speaker) const;
    std::vector<std::pair<ActOneQuest, const NpcSpeech *>> npcQuestTopics(std::string_view speaker) const;
    bool npcQuestAlert(const WorldObject &npc) const;
    std::optional<unsigned> denMonstersRemaining() const;
    bool active(Vec position) const { return simulation_.active(position); }
    const ClassicData &content() const { return content_; }
    const WorldCatalog &worldContent() const { return worldContent_; }
    const MonsterCatalog &monsterContent() const { return monsterContent_; }
    std::optional<MonsterCombatProfile> monsterCombatProfile(
        const MonsterIdentity &identity, RegionId region) const {
        return resolvedMonsterCombat(identity, region);
    }
    const auto &worldEntries() const { return worldEntries_; }
    const std::vector<ShrineStatus> &shrineStatuses() const { return shrineStatuses_; }
    uint64_t contentFingerprint() const { return contentFingerprint_; }
    const std::vector<uint64_t> &experienceThresholds() const {
        return content_.experienceByClass.at(state().player.characterClass);
    }
    uint64_t maximumExperience() const { return experienceThresholds().back(); }
    CharacterSaveData characterSave() const;
    LootState lootState() const { return loot_.snapshot(); }
    // Validate completely before replacing live state; a rejected load changes nothing.
    void restore(CharacterSaveData snapshot);
    const InventoryService &inventory() const { return inventory_; }
    const EquipmentStats &equipmentStats() const { return simulation_.equipmentStats_; }
    const CharacterAttributes &characterStats() const { return simulation_.characterStats_; }
    const std::string &characterName() const { return characterDefinition_.name; }
    const std::string &characterCode() const { return characterDefinition_.code; }
    const std::string &characterAppearance() const { return characterDefinition_.appearance; }
    unsigned bankGoldLimit() const;
    unsigned groundGoldLimit() const;
    void applyCombatEffect(ActiveCombatEffect effect);
    bool skillAvailable(int id) const;
    int effectiveSkillRank(int id) const;
    bool applyOriginalCastTiming(OriginalSkillCast &cast) const;
    int fireMasteryPercent() const;
    int lightningMasteryPercent() const;
    int coldPiercePercent() const;
    const PlayerContainers &playerContainers() const { return playerContainers_; }
    StorageAccess storage() const;
    const WorldObject *object(EntityId id) const;
    const std::vector<VendorOffer> *vendorStock(EntityId npc, bool gamble = false) const;
    bool vendorOfferSold(EntityId npc, uint32_t slot) const;
    std::optional<unsigned> vendorRepairQuote(EntityId npc, ItemHandle item) const;
    std::optional<unsigned> vendorSaleQuote(EntityId npc, ItemHandle item) const;
    unsigned vendorPurchasePrice(EntityId npc, const VendorOffer &offer, bool gamble) const;
    EntityId interactionTarget() const { return pendingInteraction_; }
    EntityId pickupTarget() const { return pickup_.id; }
    std::optional<Vec> portalPosition() const;
    std::optional<Vec> cainPortalPosition() const;
    std::array<int, 5> cainStoneSequence() const { return cainStoneOrder(); }
    bool waypointUnlocked(RegionId region) const { return state().waypoints.contains(region); }
    InventoryError previewInventory(const GameCommand &command) const;
    std::optional<GroundLocation> dropLocation() const;
    std::string debugSpawnError(std::string_view monster, Vec position) const;
    const Map &map() const { return regions_.at(current_).map; }
    const Region &region() const { return regions_.at(current_); }
    const std::vector<Region> &regions() const { return regions_; }
    int regionIndex() const { return current_; }
    std::span<const GameEvent> events() const { return simulation_.events(); }
    void submit(GameCommand command) { pending_.push_back(std::move(command)); }
    bool hasPendingCommands() const { return !pending_.empty(); }
    void tick(float dt, Vec keyboard = {}, bool forceRun = false);
};
} // namespace d2x
