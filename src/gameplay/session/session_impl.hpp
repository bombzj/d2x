#pragma once
#include "content/classic_data.hpp"
#include "core/random.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/model/interaction.hpp"
#include "gameplay/npc/store.hpp"
#include "gameplay/npc/access.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "gameplay/session/character_save.hpp"
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/model/state.hpp"
#include "world/population.hpp"
#include "world/region_store.hpp"
#include "world/maze.hpp"
#include "gameplay/areas/repository.hpp"
#include <memory>
#include <span>

#include "gameplay/session/session.hpp"
#include "gameplay/character/progression.hpp"
#include "gameplay/character/learning.hpp"
#include "gameplay/player/frame_input.hpp"
#include "gameplay/simulation/fixed_step.hpp"
#include "gameplay/skills/source.hpp"

namespace d2x {
struct QuestNpcFacts;
struct EquipmentLoadout;
class Simulation;
enum class QuestReward;
struct QuestDeathContext;
struct QuestDeathPlan;
struct DeathLootContext;
enum class QuestDeathWaveKind;
class GameSessionImpl {
    class DeathSettlementAdapter;
    struct NpcMotionState {
        EntityId id;
        Vec position, look;
        std::deque<Vec> route;
        float wait = 0;
        int target = -1;
        uint64_t random = 0;
    };
    EntityIds ids_;
    PlayerFrameInput playerInput_;
    bool dispatchCommands();
    uint64_t viewRevision_ = 1;
    uint64_t random_;
    uint64_t visualRandom_ = 0, cainRandom_ = 0;
    ClassicData content_;
    Table questObjectRows_;
    CharacterDefinition characterDefinition_;
    WorldCatalog worldContent_;
    Archives &archives_;
    RegionStore world_;
    MonsterCatalog monsterContent_;
    mutable std::map<std::pair<std::string, RegionId>, std::optional<MonsterCombatProfile>> monsterCombatCache_;
    uint64_t contentFingerprint_ = 0;
    std::optional<RegionId> denRegion_;
    std::optional<RegionId> burialRegion_;
    std::optional<RegionId> stonyRegion_, darkWoodRegion_, tristramRegion_;
    std::optional<RegionId> towerRegion_, towerCellarRegion_;
    std::optional<RegionId> barracksRegion_;
    std::optional<RegionId> catacombsFourRegion_;
    std::unique_ptr<Simulation> simulation_;
    UnitSkillSources skillSources_;
    InventoryService inventory_{ids_, content_.items, {content_.stashLayout.columns,
                                                       content_.stashLayout.rows},
                                     {content_.cubeLayout.columns, content_.cubeLayout.rows}};
    LootSystem loot_;
    ItemHandle pickup_{};
    bool pickupToCursor_ = false;
    PlayerContainers playerContainers_;
    std::vector<PlayerCorpse> playerCorpses_;
    EntityId pendingCorpse_;
    void settlePlayerDeath();
    bool completePlayerDeathAnimation();
    bool respawnPlayer();
    void beginCorpseRecovery(EntityId corpse);
    void updateCorpseRecovery();
    void recoverCorpse(PlayerCorpse &corpse);
    StorageAccess storage_;
    EntityId pendingInteraction_;
    bool pendingInteractionRepath_ = false;
    EntityId engagedNpc_;
    // D2MOO quest GUID reaction lists are local to the current game.
    std::set<std::string> pendingNpcQuestMessages_;
    EntityId jadeFigurineBoss_;
    bool jadeFigurineDropped_ = false;
    EntityId gidbinnBoss_;
    std::map<EntityId, std::vector<VendorOffer>> vendorStocks_;
    std::map<EntityId, std::set<uint32_t>> soldVendorOffers_;
    std::map<EntityId, std::vector<VendorOffer>> gambleStocks_;
    std::map<EntityId, std::vector<HirelingOffer>> hirelingOffers_;
    std::optional<uint64_t> pendingPortal_;
    bool pendingCainPortal_ = false;
    bool cowPortalOpened_ = false, uberFinaleOpened_ = false;
    std::array<bool, 3> uberPortalsOpened_{};
    std::optional<Vec> townPortalArrival_;
    std::map<RegionId, Vec> townPortalArrivals_;
    std::optional<RegionId> portalTown(RegionId field) const;
    float portalReach_ = 0;
    float cainPortalReach_ = 0;
    bool portalResources_ = false;
    std::optional<int> pendingExit_;
    std::optional<Vec> boundaryMoveTarget_;
    std::optional<LevelExit::BoundaryPassage> boundaryPassage_;
    std::vector<NpcMotionState> initialNpcMotions_;
    AreaRepository areas_;
    std::vector<ShrineStatus> shrineStatuses_;
    std::vector<GameCommand> pending_;
    int current_ = -1;
    void enter(RegionId id, std::optional<Vec> arrival = {}, std::optional<Vec> coordinateOffset = {});
    void ensureRegion(RegionId id, bool neighbours = false);
    void beginExit(int slot);
    bool questExitAllowed(RegionId destination) const;
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
    bool applyShrine(int code, EntityId source, Vec position);
    bool applySpecialShrine(const ShrineDefinition &shrine, Vec position);
    bool upgradeShrineMonster(Vec position);
    bool openShrinePortal();
    const TownPortalState *findPortal(uint64_t revision) const;
    uint64_t shrineRandom_ = 0;
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
    NpcAccess npcAccess(EntityId npc) const;
    bool deliverQuestReward(QuestReward reward, EntityId npc);
    bool exchangeQuestItem(EntityId npc, std::string_view input, std::string_view output);
    bool returnMalus(EntityId npc);
    std::optional<Vec> interactionApproach(const WorldObject &object) const;
    bool travelWaypoint(const WaypointTravel &command);
    void unlockWaypoints();
    void validateStorage();
    void closeStorage();
    void spawnLoot(std::span<const LootDrop> drops, RegionId region, Vec origin);
    void spawnDebugItem(const DebugSpawnItem &command);
    InventoryAccess inventoryAccess() const;
    EquipmentActor equipmentActor() const;
    const CharacterDefinition &definitionFor(std::string_view name) const;
    void refreshCharacter(bool fillGains = false);
    void useSkill(const UseSkill &intent);
    bool telekinesisTarget(EntityId target, int range, bool operate);
    void syncPlayerAura();
    int skillRank(const SkillRecord &skill, const CharacterState &character,
        const CharacterDefinition &definition, const CombatModifiers &bonuses,
        const EquipmentLoadout &loadout, const EquipmentActor &actor) const;
    void applyWarmth(CharacterAttributes &stats, const CharacterState &character,
                     const CharacterDefinition &definition, const InventoryService &inventory,
                     const PlayerContainers &containers, const EquipmentActor &actor) const;
    void grantExperience(uint64_t amount);
    CharacterProgressionContext characterProgressionContext();
    CharacterSkillContext characterSkillContext() const;
    void applyCharacterIntent(const CharacterIntent &intent);
    void resetCharacterAttributePoints();
    void resetCharacterSkillPoints();
    void createStarterEquipment();
    bool inventoryDestinationAllowed(const ItemDestination &destination) const;
    bool inventorySourceAllowed(EntityId item) const;
    void publishInventory(InventoryResult result, EntityId requested);
    void executeInventory(const GameCommand &command);
    void settleDeaths();
    QuestDeathContext deathQuestContext(const EnemyDied &death) const;
    void applyDeathQuest(const EnemyDied &death, const QuestDeathPlan &plan);
    void applyDeathWave(const EnemyDied &death, QuestDeathWaveKind kind);
    LootPlan planDeathLoot(const EnemyDied &death, const LootRequest &request,
        DeathLootContext context, const std::set<uint32_t> &usedUniques);
    void awardDeathExperience(const EnemyDied &death, EntityId beneficiary);
    void publishDeathLootDeferred(EntityId source, std::string_view reason);
    void onQuestRegionEntered(RegionId id);
    void updateDenQuest();
    void completeActOne(EntityId npc);
    void completeActTwo(EntityId npc);
    void activateCainQuestObject(const WorldObject &object);
    void updateQuestItems();
    void updateToolsQuestItems();
    void activateMalus(const WorldObject &object);
    void imbueWithCharsi(const ImbueItem &command);
    void socketWithLarzuk(const SocketQuestItem &command);
    void personalizeWithAnya(const PersonalizeQuestItem &command);
    void reconcileCainObjects();
    std::array<int, 5> cainStoneOrder() const;
    bool claimCainReward();
    bool claimQuestRing(int level, ItemQuality quality);
    bool assignQuestHireling(std::string_view npcClass);
    void activateLaterQuestObject(EntityId object);
    void updateLaterQuestObjects();
    void updatePrisonerObjects();
    bool activateAncientsObject(EntityId object);
    void updateAncientsObjects();
    void resetAncients();
    void completeAncientsBattle();
    void rewardAncientsExperience();
    bool activateBaalObject(EntityId object);
    void updateBaalObjects();
    bool createQuestPortal(Vec position, int objectClass, RegionId destination);
    bool translateCainScroll(EntityId npc);
    bool travelCainPortal();
    bool beginCainPortal();
    void updateCainPortal();
    void advanceHireling(float dt);
    void advanceHirelingAttack(const MonsterRecord &actor, const HirelingCombatStats &stats);
    void controlHireling(const MonsterRecord &actor, const HirelingCombatStats &stats,
                        const MonsterAttackTiming *timing, float dt);
    bool assignKashyaHireling();
    void openHirelingList(EntityId npc);
    void hireMercenary(const HireMercenary &command);
    void resurrectHireling(EntityId npc);
    void useHirelingPotion(ItemHandle item);
    InventoryError previewHirelingPotion(ItemHandle item) const;
    void equipHirelingItem(const EquipHirelingItem &command);
    InventoryError previewHirelingEquipment(const EquipHirelingItem &command) const;
    EquipmentActor hirelingEquipmentActor(std::optional<EquipmentSlot> replacedSlot) const;
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
    void transmuteCube();
    ItemGeneration questItemGeneration(std::string_view code, uint64_t &random) const;
    void activateActTwoObject(EntityId id, std::optional<ItemHandle> submitted = {});
    void updateActTwoObjects();
    struct PendingQuestNpc { RegionId region; Vec position; std::string monster; EffectFrame frame; };
    std::vector<PendingQuestNpc> pendingQuestNpcs_;
    bool questNpcConversationAllowed(const WorldObject &npc) const;
    void spawnQuestNpc(RegionId region, Vec position, std::string_view monster);
    bool spawnQuestEnemy(Vec position, std::string_view monster, std::string_view superUnique = {});
    bool openQuestPortal(EntityId npc, int objectClass, RegionId destination);
    std::optional<EffectFrame> tombOpeningFrame_;
    std::optional<EffectFrame> tombCollapseFrame_;
    std::optional<EffectFrame> sunDarkeningFrame_;
    void beginPortal(uint64_t revision);
    void updatePortal();
    InventoryError previewPortalScroll(ItemHandle item) const;
    void useBeltColumn(int column, bool hireling);
    void beginPickup(ItemHandle item, bool toCursor);
    void updatePickup();
    void cancelPickup();
    int validateCharacterRestore(const CharacterSaveData &data) const;
    CharacterSaveData prepareCharacterRestore(CharacterSaveData character) const;
    void validateItemProperties(const CharacterSaveData &snapshot) const;
    void configureSkillSources();
    void spawnDebugMonster(const DebugSpawnMonster &command);
    void damageDebugMonster(const DebugDamageMonster &command);
    std::optional<MonsterCombatProfile> resolvedMonsterCombat(
        const MonsterIdentity &identity, RegionId region,
        const MonsterEnchantment *enchantment = nullptr) const;

  public:
    uint64_t viewRevision() const { return viewRevision_; }
    bool carriesQuestItem(std::string_view code) const;
    bool questItemOnGround(std::string_view code) const;
    bool canInsertStaff(EntityId id) const;
    bool canHireFrom(EntityId npc) const;
    bool canResurrectHireling(EntityId npc) const;
    unsigned hirelingResurrectionCost() const;
    const std::vector<HirelingOffer> *hirelingOffers(EntityId npc) const;
    const HirelingDefinition *hirelingDefinition() const;
    HirelingCombatStats hirelingStats() const;
    static constexpr float fixedStep = gameFixedStep;
    GameSessionImpl(Archives &archives, const WorldSelection &selection, int startRegion,
                uint32_t sessionSeed, PopulationSettings population,
                std::string characterClass = "Barbarian", std::string characterName = "Hero");
    ~GameSessionImpl();
    GameSessionImpl(const GameSessionImpl &) = delete;
    GameSessionImpl &operator=(const GameSessionImpl &) = delete;
    uint64_t visualSeed() const { return visualRandom_; }
    const WorldState &state() const;
    void setRunning(bool running);
    const QuestRecord &quest(QuestId id, int difficulty) const {
        return state().player.character.quests.at(size_t(difficulty)).at(questIndex(id));
    }
    const QuestRecord &quest(QuestId id) const { return quest(id, state().population.difficulty); }
    QuestNpcFacts questNpcFacts(const WorldObject &npc) const;
    NpcQuestDialogue npcQuestDialogue(EntityId npc) const;
    std::vector<std::pair<QuestId, const NpcSpeech *>> npcQuestTopics(EntityId npc) const;
    bool npcQuestAlert(const WorldObject &npc) const;
    std::optional<unsigned> denMonstersRemaining() const;
    bool usableCorpse(EntityId id) const;
    Vec combatPosition(EntityId id) const;
    bool canAttack(EntityId actor, EntityId target) const;
    bool active(Vec position) const;
    const ClassicData &content() const { return content_; }
    const WorldCatalog &worldContent() const { return worldContent_; }
    const MonsterCatalog &monsterContent() const { return monsterContent_; }
    std::optional<MonsterCombatProfile> monsterCombatProfile(EntityId actor, RegionId region) const;
    const auto &worldEntries() const { return world_.entries(); }
    const std::vector<ShrineStatus> &shrineStatuses() const { return shrineStatuses_; }
    uint64_t contentFingerprint() const { return contentFingerprint_; }
    const std::vector<uint64_t> &experienceThresholds() const {
        return content_.experienceByClass.at(state().player.character.characterClass);
    }
    uint64_t maximumExperience() const { return experienceThresholds().back(); }
    CharacterSaveData characterSave() const;
    LootState lootState() const { return loot_.snapshot(); }
    // Validate completely before replacing live state; a rejected load changes nothing.
    void restore(CharacterSaveData snapshot);
    const InventoryService &inventory() const { return inventory_; }
    const EquipmentStats &equipmentStats() const { return state().player.equipment; }
    const ItemInstance *usableEquipment(EquipmentSlot slot) const;
    const CharacterAttributes &characterStats() const { return state().player.attributes; }
    const std::string &characterName() const { return characterDefinition_.name; }
    const std::string &characterCode() const { return characterDefinition_.code; }
    const std::string &characterAppearance() const { return characterDefinition_.appearance; }
    unsigned bankGoldLimit() const;
    unsigned groundGoldLimit() const;
    bool skillAvailable(int id) const;
    bool canAllocateSkill(int id) const;
    int nextSkillRequiredLevel(int id) const;
    bool weaponSkillReady(const SkillCastSpec &skill) const;
    int effectiveSkillRank(int id) const;
    bool applySkillCastTiming(SkillCastSpec &cast) const;
    int fireMasteryPercent() const;
    int lightningMasteryPercent() const;
    int coldPiercePercent() const;
    const PlayerContainers &playerContainers() const { return playerContainers_; }
    std::span<const PlayerCorpse> playerCorpses() const { return playerCorpses_; }
    StorageAccess storage() const;
    const WorldObject *object(EntityId id) const;
    const std::vector<VendorOffer> *vendorStock(EntityId npc, bool gamble = false) const;
    bool vendorOfferSold(EntityId npc, uint32_t slot) const;
    std::optional<unsigned> vendorRepairQuote(EntityId npc, ItemHandle item) const;
    std::optional<unsigned> vendorSaleQuote(EntityId npc, ItemHandle item) const;
    unsigned vendorPurchasePrice(EntityId npc, const VendorOffer &offer, bool gamble) const;
    EntityId interactionTarget() const { return pendingInteraction_; }
    const ItemInstance *cursorItem() const {
        return inventory_.item(inventory_.itemAt(playerContainers_.cursor, {}));
    }
    EntityId pickupTarget() const { return pickup_.id; }
    using PortalView = SessionPortalView;
    std::vector<PortalView> portals(RegionId region) const;
    std::optional<Vec> portalPosition() const;
    std::optional<Vec> cainPortalPosition() const;
    std::array<int, 5> cainStoneSequence() const { return cainStoneOrder(); }
    bool waypointUnlocked(RegionId region) const { return state().waypoints.contains(region); }
    InventoryError previewInventory(const GameCommand &command) const;
    std::optional<GroundLocation> dropLocation() const;
    std::string debugSpawnError(std::string_view monster, Vec position) const;
    const Map &map() const { return world_.at(current_).map; }
    const Region &region() const { return world_.at(current_); }
    const std::vector<Region> &regions() const { return world_.regions(); }
    int regionIndex() const { return current_; }
    // Current area and areas joined by continuous ground, in current-area coordinates.
    std::vector<std::pair<int, Vec>> sceneRegions() const;
    const AreaState &areaState(int index) const {
        return index == current_ ? state().area : areas_.read(size_t(index));
    }
    bool roomVisible(int index, Vec position) const;
    std::span<const GameEvent> events() const;
    void submit(GameCommand command) { pending_.push_back(std::move(command)); }
    bool hasPendingCommands() const { return !pending_.empty(); }
    bool setPlayerInput(PlayerFrameInput input);
    void advance(float dt);
    void tick(float dt, Vec keyboard = {}, bool forceRun = false);
};
} // namespace d2x
