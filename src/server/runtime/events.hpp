#pragma once
#include "contracts.hpp"
#include "server/game_commands.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/quest/id.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/units/resources.hpp"
#include <deque>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace d2x::server {
struct DeathFact { uint64_t occurrence{}; EntityId victim, killer; RegionId area; uint32_t goldLost{}; };
struct AttackFact { EntityId actor, target; uint8_t actorType{}, targetType{}; RegionId area; Vec position, destination; uint64_t action{}; uint16_t skill{}; uint8_t rank{1}; bool forced{}; uint8_t monsterMode{4}; };
// Native life scale: type 0 uses 100, type 1 uses raw 128. Monster 0C bit 7
// carries the lightning-enchanted emission flag, independently of rank.
struct HitFact { EntityId target; uint8_t type{}; RegionId area; uint8_t life{}; bool killed{}; Vec position; uint8_t hitClass{}, monsterMode{}; std::optional<Vec> knockback{}; bool lightningReady{}; uint8_t playerMode{}; std::optional<MotionTarget> actionTarget{}; };
struct SoundFact { EntityId actor; uint8_t type{}; RegionId area; uint16_t sound{}; bool directSound{}; };
struct OverlayFact { EntityId actor; uint8_t type{}; RegionId area; int overlay{}; };
struct QuestStatusFact {QuestId quest;uint8_t status{};uint16_t counter{};};
struct QuestEffectFact {uint8_t effect{};};
struct MonsterClassHintFact {EntityId actor;RegionId area;uint16_t nativeClass{};};
struct Pd2MapAttributesFact {EntityId portal;RegionId area;uint32_t itemIndex{};uint8_t quality{};std::vector<StatValue> attributes;};
struct LifeFact { EntityId actor; Fixed8 life; };
struct ManaFact { EntityId actor; Fixed8 mana; };
// Resource-only commits do not clone equipment/passive graphs into output.
// Before/after values keep native delta encoding independent of later writes.
struct ResourcesFact { EntityId actor; ResourceValues before,after; };
struct RepositionFact { EntityId actor; RegionId area; Vec position; bool notifyOwner=true; };
struct StateFact { EntityId actor; uint8_t type{}; RegionId area; int state{}; bool enabled{}; std::vector<StatValue> stats{}; };
struct SkillPulseFact { EntityId owner, target; uint8_t ownerType{}, targetType{1}; RegionId area; int skill{}, rank{}; Vec position; uint8_t parameter{};uint32_t argumentX{},argumentY{}; };
struct SkillEndFact {EntityId actor;uint8_t type{};RegionId area;uint16_t skill{};};
struct ItemSkillFact { EntityId owner,target; uint8_t targetType{1}; RegionId area; int skill{},rank{}; Vec position; uint16_t flags{}; uint8_t ownerType{}; };
struct MissileFact { EntityId owner; uint8_t ownerType{}; RegionId area; int definition{}, rank{}, frame{}; Vec position, destination; uint8_t pierce{}; EntityId missile{}; bool forcePublication{}; };
struct ItemTargetingFact { EntityId source; int cursor{-1}; int skill{-1}; };
struct ItemFact { TransactionId transaction; ItemChange change; };
// Sparse immutable projection captured at commit. Encoding never consults a
// later live inventory: several commands can commit before output is drained.
struct InventoryFact {
    PersistentCharacter projection;
    std::vector<ItemChange> changes;
    bool switchedWeapons{};
};
struct QuestFact { PlayerId player; CharacterRecord record; int difficulty{}; unsigned remaining{}; std::array<int,5> stones{}; std::optional<uint16_t> tombOffset{}; std::optional<bool> eclipse{}; std::optional<uint8_t> staffStatus{}; std::optional<uint8_t> khalimStatus{}; unsigned rescued{}; std::array<uint16_t,48> gameFlags{}; std::array<bool,size_t(QuestId::Count)> unavailable{}, credited{}, entitled{}; bool fullStatus{}; std::optional<uint16_t> zooMonster{}; bool zooOnly{}; };
struct AttributeFact { EntityId unit; uint64_t revision{}; };
struct EnvironmentFact {int act{},cycle{},ticks{};bool eclipse{};};
struct CharacterFact {
    CharacterRecord before, after;
    attributes::Totals previous, current;
    std::optional<bool> selectedHand{}; // Explicit selection also confirms an unchanged value.
    bool explicitResources{}; // Native UpdateAttribute callbacks; ordinary changes use compact sampling.
};
struct TravelFact { PlayerId player; EntityId actor; RegionId from, to; uint64_t areaGeneration{}; Vec position; bool walking{}, revived{}; };
// Frozen at the authoritative frame boundary, before later ENDANIM callbacks.
// Initial visibility uses a current object snapshot instead of replaying this.
struct ObjectFact {
    EntityId object, actor; RegionId area; uint64_t revision{};
    Vec position; int mode{}; bool targetable{}; std::optional<int> shrine{};
};
struct ChatFact { EntityId actor; std::string name, text; std::vector<PlayerId> recipients; uint8_t type{1}, language{}, unitType{2}, nameColor{},messageColor{}; };
struct CommandFact { PlayerId player; uint64_t sequence{}; CommandStatus result; };
struct NpcMessage { uint8_t menu{}; uint16_t text{}; };
struct NpcMessagesFact { EntityId npc; std::vector<NpcMessage> messages; uint8_t type=1; bool activate=true; };
struct MerchantFact { EntityId npc, item; uint8_t operation{}, result{}; uint32_t gold{}; bool refreshShop{}; };
struct WaypointFact { EntityId source; std::vector<RegionId> unlocked; };
struct UiFact { uint8_t action{}; };
struct NpcServiceFact { EntityId npc; uint8_t result{}, state{}; };
// Captured at the drop transaction; later visibility snapshots are not drops.
struct GroundDropFact { ItemInstance item; };
struct GroundRemoveFact { EntityId item; };
struct GroundRestoredFact {EntityId item;uint64_t revision{};};
struct HirelingListFact { EntityId npc; std::vector<std::pair<uint16_t,uint32_t>> offers; };
// Private native trade UI and offer projection; mirror items never own inventory.
struct TradeFact {
    std::optional<uint8_t> action{}; // Gold acknowledgements do not reopen/reset the UI.
    EntityId partner{};
    std::string name{};
    std::optional<unsigned> ownGold{}, peerGold{};
};
struct TradeItemsFact { PersistentCharacter projection; std::vector<EntityId> removed; };
struct PlayerMessageFact { uint8_t type{}; std::string name; std::string other{}; uint32_t value{}; uint8_t parameter{}; std::string characterClass{}; };
struct ChatRelationFact { EntityId from,to; uint16_t flags{}, reverse{}, fromLevel{}, toLevel{}; };
struct PartyNoticeFact {PlayerId recipient;EntityId player;uint8_t action{};};
using DomainFact = std::variant<ManaFact, RepositionFact, LifeFact, AttackFact, HitFact, DeathFact, ItemFact, InventoryFact, CharacterFact, QuestFact, AttributeFact, TravelFact, ObjectFact, ChatFact, CommandFact, NpcMessagesFact, MerchantFact, UiFact, WaypointFact, StateFact, MissileFact, SkillPulseFact, SoundFact, OverlayFact, ItemTargetingFact, GroundDropFact, NpcServiceFact, GroundRemoveFact, ItemSkillFact, GroundRestoredFact, HirelingListFact, TradeFact, TradeItemsFact, PlayerMessageFact, ChatRelationFact, PartyNoticeFact, EnvironmentFact, ResourcesFact, SkillEndFact, QuestStatusFact, QuestEffectFact, MonsterClassHintFact, Pd2MapAttributesFact>;
// Compact bounded observation, independent of reliable delivery and acknowledgement.
struct DiagnosticEvent {
    uint64_t sequence{}, batch{}, tick{}, transaction{};
    size_t type{};
    EntityId actor, target;
    RegionId area{};
    int64_t value{}, secondary{};
    Vec position;
};
// Scratch facts for later phases in the SAME step, not a deferred event bus.
// Work for a phase that has already run must stay in its owner's pending state.
class FrameFacts {
    std::vector<DomainFact> values_;
  public:
    std::span<const DomainFact> read() const { return values_; }
    DomainResult<> append(DomainFact);
};
enum class AudienceKind { Player, Area, Game };
struct Audience { AudienceKind kind; PlayerId player; RegionId area; };
struct EventBatch {
    uint64_t sequence{}, tick{};
    TransactionId transaction;
    Audience audience;
    std::vector<DomainFact> facts;
};
// Reliable authority-to-host output. Queries do not consume it. Acknowledgement
// follows successful native encoding/delivery preparation, not merely a read.
class EventOutbox {
    std::deque<EventBatch> batches_;
    size_t facts_{};
    uint64_t next_ = 1, acknowledged_{};
    std::array<DiagnosticEvent, 1024> history_{};
    uint64_t historyNext_ = 1;
    void observe(const EventBatch &) noexcept;
  public:
    bool hasCapacity(size_t count, size_t batches = 1) const;
    DomainResult<uint64_t> publish(EventBatch);
    DomainResult<uint64_t> publishGroup(std::vector<EventBatch>);
    const std::deque<EventBatch> &pending() const { return batches_; }
    bool acknowledge(uint64_t sequence);
    uint64_t historyFirst() const { return historyNext_ > history_.size() ? historyNext_ - history_.size() : 1; }
    uint64_t historyLast() const { return historyNext_ - 1; }
    std::vector<DiagnosticEvent> history(uint64_t since, size_t limit) const;
};
}
