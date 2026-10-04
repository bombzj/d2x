#include "gameplay/session/session_impl.hpp"
#include <utility>

namespace d2x {
uint64_t GameSession::viewRevision() const { return impl_->viewRevision(); }
GameSession::GameSession(Archives &archives, const WorldSelection &selection, int startRegion,
                        uint32_t sessionSeed, PopulationSettings population,
                        std::string characterClass, std::string characterName)
    : impl_(std::make_unique<GameSessionImpl>(archives, selection, startRegion, sessionSeed,
                                             population, std::move(characterClass), std::move(characterName))) {}
GameSession::~GameSession() = default;
bool GameSession::carriesQuestItem(std::string_view code) const { return impl_->carriesQuestItem(code); }
bool GameSession::questItemOnGround(std::string_view code) const { return impl_->questItemOnGround(code); }
bool GameSession::canInsertStaff(EntityId id) const { return impl_->canInsertStaff(id); }
bool GameSession::canHireFrom(EntityId npc) const { return impl_->canHireFrom(npc); }
bool GameSession::canResurrectHireling(EntityId npc) const { return impl_->canResurrectHireling(npc); }
unsigned GameSession::hirelingResurrectionCost() const { return impl_->hirelingResurrectionCost(); }
const std::vector<HirelingOffer> * GameSession::hirelingOffers(EntityId npc) const { return impl_->hirelingOffers(npc); }
const HirelingDefinition * GameSession::hirelingDefinition() const { return impl_->hirelingDefinition(); }
HirelingCombatStats GameSession::hirelingStats() const { return impl_->hirelingStats(); }
uint64_t GameSession::visualSeed() const { return impl_->visualSeed(); }
const WorldState & GameSession::state() const { return impl_->state(); }
void GameSession::setRunning(bool running) { impl_->setRunning(running); }
const QuestRecord & GameSession::quest(QuestId id, int difficulty) const { return impl_->quest(id, difficulty); }
const QuestRecord & GameSession::quest(QuestId id) const { return impl_->quest(id); }
NpcQuestDialogue GameSession::npcQuestDialogue(EntityId npc) const { return impl_->npcQuestDialogue(npc); }
std::vector<std::pair<QuestId, const NpcSpeech *>> GameSession::npcQuestTopics(EntityId npc) const { return impl_->npcQuestTopics(npc); }
bool GameSession::npcQuestAlert(const WorldObject &npc) const { return impl_->npcQuestAlert(npc); }
std::optional<unsigned> GameSession::denMonstersRemaining() const { return impl_->denMonstersRemaining(); }
bool GameSession::usableCorpse(EntityId id) const { return impl_->usableCorpse(id); }
Vec GameSession::combatPosition(EntityId id) const { return impl_->combatPosition(id); }
bool GameSession::canAttack(EntityId actor, EntityId target) const { return impl_->canAttack(actor, target); }
bool GameSession::active(Vec position) const { return impl_->active(position); }
const ClassicData & GameSession::content() const { return impl_->content(); }
const WorldCatalog & GameSession::worldContent() const { return impl_->worldContent(); }
const MonsterCatalog & GameSession::monsterContent() const { return impl_->monsterContent(); }
std::optional<MonsterCombatProfile> GameSession::monsterCombatProfile(EntityId actor, RegionId region) const { return impl_->monsterCombatProfile(actor, region); }
const std::vector<WorldEntry> & GameSession::worldEntries() const { return impl_->worldEntries(); }
const std::vector<ShrineStatus> & GameSession::shrineStatuses() const { return impl_->shrineStatuses(); }
uint64_t GameSession::contentFingerprint() const { return impl_->contentFingerprint(); }
const std::vector<uint64_t> & GameSession::experienceThresholds() const { return impl_->experienceThresholds(); }
uint64_t GameSession::maximumExperience() const { return impl_->maximumExperience(); }
CharacterSaveData GameSession::characterSave() const { return impl_->characterSave(); }
LootState GameSession::lootState() const { return impl_->lootState(); }
void GameSession::restore(CharacterSaveData snapshot) { impl_->restore(std::move(snapshot)); }
const InventoryService & GameSession::inventory() const { return impl_->inventory(); }
const EquipmentStats & GameSession::equipmentStats() const { return impl_->equipmentStats(); }
const ItemInstance * GameSession::usableEquipment(EquipmentSlot slot) const { return impl_->usableEquipment(slot); }
const CharacterAttributes & GameSession::characterStats() const { return impl_->characterStats(); }
const std::string & GameSession::characterName() const { return impl_->characterName(); }
const std::string & GameSession::characterCode() const { return impl_->characterCode(); }
const std::string & GameSession::characterAppearance() const { return impl_->characterAppearance(); }
unsigned GameSession::bankGoldLimit() const { return impl_->bankGoldLimit(); }
unsigned GameSession::groundGoldLimit() const { return impl_->groundGoldLimit(); }
bool GameSession::skillAvailable(int id) const { return impl_->skillAvailable(id); }
bool GameSession::canAllocateSkill(int id) const { return impl_->canAllocateSkill(id); }
int GameSession::nextSkillRequiredLevel(int id) const { return impl_->nextSkillRequiredLevel(id); }
bool GameSession::weaponSkillReady(const SkillCastSpec &skill) const { return impl_->weaponSkillReady(skill); }
int GameSession::effectiveSkillRank(int id) const { return impl_->effectiveSkillRank(id); }
bool GameSession::applySkillCastTiming(SkillCastSpec &cast) const { return impl_->applySkillCastTiming(cast); }
int GameSession::fireMasteryPercent() const { return impl_->fireMasteryPercent(); }
int GameSession::lightningMasteryPercent() const { return impl_->lightningMasteryPercent(); }
int GameSession::coldPiercePercent() const { return impl_->coldPiercePercent(); }
const PlayerContainers & GameSession::playerContainers() const { return impl_->playerContainers(); }
StorageAccess GameSession::storage() const { return impl_->storage(); }
const WorldObject * GameSession::object(EntityId id) const { return impl_->object(id); }
const std::vector<VendorOffer> * GameSession::vendorStock(EntityId npc, bool gamble) const { return impl_->vendorStock(npc, gamble); }
bool GameSession::vendorOfferSold(EntityId npc, uint32_t slot) const { return impl_->vendorOfferSold(npc, slot); }
std::optional<unsigned> GameSession::vendorRepairQuote(EntityId npc, ItemHandle item) const { return impl_->vendorRepairQuote(npc, item); }
std::optional<unsigned> GameSession::vendorSaleQuote(EntityId npc, ItemHandle item) const { return impl_->vendorSaleQuote(npc, item); }
unsigned GameSession::vendorPurchasePrice(EntityId npc, const VendorOffer &offer, bool gamble) const { return impl_->vendorPurchasePrice(npc, offer, gamble); }
EntityId GameSession::interactionTarget() const { return impl_->interactionTarget(); }
const ItemInstance * GameSession::cursorItem() const { return impl_->cursorItem(); }
EntityId GameSession::pickupTarget() const { return impl_->pickupTarget(); }
std::vector<GameSession::PortalView> GameSession::portals(RegionId region) const { return impl_->portals(region); }
std::optional<Vec> GameSession::portalPosition() const { return impl_->portalPosition(); }
std::optional<Vec> GameSession::cainPortalPosition() const { return impl_->cainPortalPosition(); }
std::array<int, 5> GameSession::cainStoneSequence() const { return impl_->cainStoneSequence(); }
bool GameSession::waypointUnlocked(RegionId region) const { return impl_->waypointUnlocked(region); }
InventoryError GameSession::previewInventory(const GameCommand &command) const { return impl_->previewInventory(command); }
std::optional<GroundLocation> GameSession::dropLocation() const { return impl_->dropLocation(); }
std::string GameSession::debugSpawnError(std::string_view monster, Vec position) const { return impl_->debugSpawnError(monster, position); }
const Map & GameSession::map() const { return impl_->map(); }
const Region & GameSession::region() const { return impl_->region(); }
const std::vector<Region> & GameSession::regions() const { return impl_->regions(); }
int GameSession::regionIndex() const { return impl_->regionIndex(); }
std::vector<std::pair<int, Vec>> GameSession::sceneRegions() const { return impl_->sceneRegions(); }
const AreaState & GameSession::areaState(int index) const { return impl_->areaState(index); }
bool GameSession::roomVisible(int index, Vec position) const { return impl_->roomVisible(index, position); }
std::span<const GameEvent> GameSession::events() const { return impl_->events(); }
void GameSession::submit(GameCommand command) { impl_->submit(std::move(command)); }
bool GameSession::hasPendingCommands() const { return impl_->hasPendingCommands(); }
bool GameSession::setPlayerInput(PlayerFrameInput input) { return impl_->setPlayerInput(input); }
void GameSession::advance(float dt) { impl_->advance(dt); }
void GameSession::tick(float dt, Vec keyboard, bool forceRun) { impl_->tick(dt, keyboard, forceRun); }
} // namespace d2x
