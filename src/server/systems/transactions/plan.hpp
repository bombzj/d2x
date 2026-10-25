#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/items/state.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "gameplay/units/resources.hpp"
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace d2x::server::effects {struct PlayerEffectsPublication;struct AuraUnitPublication;struct PlayerPoisonCandidate;}
namespace d2x {struct FrameEventPublication;}
namespace d2x::server::missiles {class NativeBirthPublication;}
namespace d2x::server {struct RoomRandomLoan;}
namespace d2x::server::transactions {
using InitialEffectMutation=std::function<void(CombatEffectSet &,const attributes::AuraSourceObserver &,const attributes::AuraSourceReader &,effects::PlayerPoisonCandidate &)>;
struct ItemTransfer { std::optional<PlayerId> from, to; ItemHandle item; ItemDestination destination; };
struct Reward { PlayerId player; uint64_t sourceOccurrence{}, experience{}; unsigned gold{}; };
struct Exchange { PlayerId first, second; std::vector<ItemTransfer> items; unsigned firstGold{}, secondGold{}; };
enum class ResourceRefresh { Clamp, AttributeGain, LevelUp, StaminaPotion };
struct WorldEdit { uint64_t expected{}; items::State next; };
struct DirectedFacts { PlayerId player; std::vector<DomainFact> facts; };
struct UnitRandomEdit {uint64_t expected{},next{};};
// The candidate has already deducted this ordinary skill cost from mana.
// This marker permits the native Blood Mana procedure; damage/drain and
// inventory resource changes do not pass through skill consumption.
struct SkillManaPayment {Fixed8 amount;int skill{-1};bool aura{};};
struct InventoryEdit {
    ActorContext actor;
    uint64_t expectedRevision{}, expectedCharacterRevision{};
    InventoryState inventory;
    std::vector<ItemChange> changes;
    unsigned weaponSet{};
    std::shared_ptr<const EquipmentRules> equipment{};
    std::optional<WorldEdit> world{};
    std::optional<CharacterRecord> character{};
    std::optional<TransientAttributes> transient{};
    std::optional<std::vector<PlayerCorpse>> corpses{};
    ResourceRefresh resources = ResourceRefresh::Clamp;
    std::vector<DomainFact> facts{};
    std::vector<DomainFact> publicFacts{};
    std::optional<PointTarget> knockback{};
    std::optional<SkillCharge> charge{};
    std::vector<DirectedFacts> directed{};
    bool playerTrade{};
    std::optional<EntityId> lootedCorpse{}; // Paired owner/recipient inventory transaction.
    std::optional<std::optional<ItemInstance>> ironGolem{};
    std::vector<ResourceMaximums> resourceMaximums{};
    std::optional<ResourceMaximums> resourceBase{};
    std::optional<UnitRandomEdit> random{};
    std::optional<SkillManaPayment> skillPayment{};
    bool explicitResources{};
};
struct ItemAuraTimerCompletion {
    EntityId source;
    std::optional<ItemAuraTimer> replacement; // Absent cancels that source's PeriodicStats events.
};
struct CharacterEdit {
    ActorContext actor;
    uint64_t expectedInventoryRevision{}, expectedCharacterRevision{};
    CharacterRecord player;
    ResourceRefresh resources = ResourceRefresh::Clamp;
    uint64_t experienceAward{};
    std::optional<TransientAttributes> transient{};
    std::optional<PointTarget> revival{};
    std::optional<std::map<RegionId,float>> waypoints{};
    std::optional<bool> selectedHand{};
    std::vector<DomainFact> publicFacts{};
    std::vector<DomainFact> facts{};
    std::optional<PointTarget> knockback{};
    std::optional<SkillCharge> charge{};
    std::shared_ptr<const EquipmentRules> equipment{};
    std::optional<std::optional<ItemInstance>> ironGolem{};
    std::vector<ResourceMaximums> resourceMaximums{};
    // Candidate resources may already include callback effects before a later
    // recovery pulse; this supplies their current cap for the final refresh.
    std::optional<ResourceMaximums> resourceBase{};
    bool refreshEquipment{}; // Native explicit UpdateInventoryItems boundary.
    std::optional<ItemAuraTimerCompletion> itemAuraTimer{};
    std::optional<UnitRandomEdit> random{};
    bool cancelItemAuras{};
    std::optional<SkillManaPayment> skillPayment{};
    // Prepared destination for source-controlled region effects. The travel
    // owner publishes placement only after this character transaction commits.
    std::optional<RegionId> environmentRegion{};
    bool explicitResources{};
};
using Change = std::variant<ItemTransfer, Reward, Exchange, InventoryEdit, CharacterEdit>;
struct PreparedPlayer {
    PersistentCharacter persistent;
    attributes::Totals totals;
    std::vector<ItemChange> changes;
    bool switchedWeapons{}, inventoryChanged{};
};
struct Plan { TransactionId id; std::vector<RevisionGuard> expected; Change change; std::optional<PreparedPlayer> player;
    std::shared_ptr<effects::PlayerEffectsPublication> effects{};std::vector<Plan> auraPlayers{};
    std::shared_ptr<effects::AuraUnitPublication> auraUnits{};bool auraRecipient{};std::optional<UnitRandomEdit> random{};
    std::shared_ptr<FrameEventPublication> periodicEvents{};
    std::shared_ptr<missiles::NativeBirthPublication> missileBirth{};
    std::shared_ptr<const RoomRandomLoan> roomRandom{};
    std::vector<std::shared_ptr<const RoomRandomLoan>> roomRandomLoans{};
    std::optional<UnitRandomEdit> gameRandom{}; };
}
