#pragma once
#include "gameplay/monsters/population_settings.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "gameplay/model/commands.hpp"
#include "gameplay/model/events.hpp"
#include "gameplay/model/interaction.hpp"
#include "gameplay/session/session_views.hpp"
#include "gameplay/player/frame_input.hpp"
#include "gameplay/simulation/fixed_step.hpp"
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2x {
class Archives;
class GameSessionImpl;
class InventoryService;
class MonsterCatalog;
class WorldCatalog;
struct AreaState;
struct CharacterSaveData;
struct ClassicData;
struct Map;
struct SkillCastSpec;
struct LootState;
struct WorldEntry;
struct WorldObject;
struct WorldSelection;
struct WorldState;
struct Region;
struct VendorOffer;

// Compatibility facade. Private runtime ownership is isolated from consumers;
// complete state/content queries will be replaced by client projections in P1.
class GameSession {
    std::unique_ptr<GameSessionImpl> impl_;

  public:
    // Local projection cache token; changes after a completed authority update.
    uint64_t viewRevision() const;
    bool carriesQuestItem(std::string_view code) const;
    bool canInsertStaff(EntityId id) const;
    bool canHireFrom(EntityId npc) const;
    bool canResurrectHireling(EntityId npc) const;
    unsigned hirelingResurrectionCost() const;
    const std::vector<HirelingOffer> *hirelingOffers(EntityId npc) const;
    const HirelingDefinition *hirelingDefinition() const;
    HirelingCombatStats hirelingStats() const;
    static constexpr float fixedStep = gameFixedStep;
    GameSession(Archives &archives, const WorldSelection &selection, int startRegion,
                uint32_t sessionSeed, PopulationSettings population,
                std::string characterClass = "Barbarian", std::string characterName = "Hero");
    ~GameSession();
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;
    uint64_t visualSeed() const;
    const WorldState &state() const;
    void setRunning(bool running);
    const QuestRecord &quest(ActOneQuest id, int difficulty) const;
    const QuestRecord &quest(ActOneQuest id) const;
    NpcQuestDialogue npcQuestDialogue(std::string_view speaker) const;
    std::vector<std::pair<ActOneQuest, const NpcSpeech *>> npcQuestTopics(std::string_view speaker) const;
    bool npcQuestAlert(const WorldObject &npc) const;
    std::optional<unsigned> denMonstersRemaining() const;
    bool usableCorpse(EntityId id) const;
    Vec combatPosition(EntityId id) const;
    bool canAttack(EntityId actor, EntityId target) const;
    bool active(Vec position) const;
    const ClassicData &content() const;
    const WorldCatalog &worldContent() const;
    const MonsterCatalog &monsterContent() const;
    std::optional<MonsterCombatProfile> monsterCombatProfile(
        EntityId actor, RegionId region) const;
    const std::vector<WorldEntry> &worldEntries() const;
    const std::vector<ShrineStatus> &shrineStatuses() const;
    uint64_t contentFingerprint() const;
    const std::vector<uint64_t> &experienceThresholds() const;
    uint64_t maximumExperience() const;
    CharacterSaveData characterSave() const;
    LootState lootState() const;
    // Validate completely before replacing live state; a rejected load changes nothing.
    void restore(CharacterSaveData snapshot);
    const InventoryService &inventory() const;
    const EquipmentStats &equipmentStats() const;
    const ItemInstance *usableEquipment(EquipmentSlot slot) const;
    const CharacterAttributes &characterStats() const;
    const std::string &characterName() const;
    const std::string &characterCode() const;
    const std::string &characterAppearance() const;
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
    const PlayerContainers &playerContainers() const;
    StorageAccess storage() const;
    const WorldObject *object(EntityId id) const;
    const std::vector<VendorOffer> *vendorStock(EntityId npc, bool gamble = false) const;
    bool vendorOfferSold(EntityId npc, uint32_t slot) const;
    std::optional<unsigned> vendorRepairQuote(EntityId npc, ItemHandle item) const;
    std::optional<unsigned> vendorSaleQuote(EntityId npc, ItemHandle item) const;
    unsigned vendorPurchasePrice(EntityId npc, const VendorOffer &offer, bool gamble) const;
    EntityId interactionTarget() const;
    const ItemInstance *cursorItem() const;
    EntityId pickupTarget() const;
    using PortalView = SessionPortalView;
    std::vector<PortalView> portals(RegionId region) const;
    std::optional<Vec> portalPosition() const;
    std::optional<Vec> cainPortalPosition() const;
    std::array<int, 5> cainStoneSequence() const;
    bool waypointUnlocked(RegionId region) const;
    InventoryError previewInventory(const GameCommand &command) const;
    std::optional<GroundLocation> dropLocation() const;
    std::string debugSpawnError(std::string_view monster, Vec position) const;
    const Map &map() const;
    const Region &region() const;
    const std::vector<Region> &regions() const;
    int regionIndex() const;
    // Current area and areas joined by continuous ground, in current-area coordinates.
    std::vector<std::pair<int, Vec>> sceneRegions() const;
    const AreaState &areaState(int index) const;
    bool roomVisible(int index, Vec position) const;
    std::span<const GameEvent> events() const;
    void submit(GameCommand command);
    bool hasPendingCommands() const;
    bool setPlayerInput(PlayerFrameInput input);
    void advance(float dt);
    // Host/debug compatibility: explicit control for this call, neutral by default.
    void tick(float dt, Vec keyboard = {}, bool forceRun = false);
};
} // namespace d2x
