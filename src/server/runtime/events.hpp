#pragma once
#include "contracts.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/quest/id.hpp"
#include "gameplay/character/persistent_character.hpp"
#include <deque>
#include <array>
#include <span>
#include <vector>

namespace d2x::server {
struct DeathFact { uint64_t occurrence{}; EntityId victim, killer; RegionId area; };
struct AttackFact { EntityId actor, target; uint8_t actorType{}, targetType{}; RegionId area; Vec position, destination; uint64_t action{}; uint16_t skill{}; uint8_t rank{1}; bool forced{}; uint8_t monsterMode{4}; };
// Native life scale: type 0 uses 100, type 1 uses raw 128. Monster 0C bit 7
// carries the lightning-enchanted emission flag, independently of rank.
struct HitFact { EntityId target; uint8_t type{}; RegionId area; uint8_t life{}; bool killed{}; Vec position; uint8_t hitClass{}, monsterMode{}; std::optional<Vec> knockback{}; bool lightningReady{}; };
struct SoundFact { EntityId actor; uint8_t type{}; RegionId area; uint8_t sound{}; };
struct OverlayFact { EntityId actor; uint8_t type{}; RegionId area; int overlay{}; };
struct LifeFact { EntityId actor; float life{}; };
struct ManaFact { EntityId actor; float mana{}; };
struct RepositionFact { EntityId actor; RegionId area; Vec position; };
struct StateFact { EntityId actor; uint8_t type{}; RegionId area; int state{}; bool enabled{}; std::vector<std::pair<int,int64_t>> stats{}; };
struct SkillPulseFact { EntityId owner, target; uint8_t ownerType{}, targetType{1}; RegionId area; int skill{}, rank{}; Vec position; };
struct ItemSkillFact { EntityId owner,target; uint8_t targetType{1}; RegionId area; int skill{},rank{}; Vec position; uint16_t flags{}; };
struct MissileFact { EntityId owner; uint8_t ownerType{}; RegionId area; int definition{}, rank{}, frame{}; Vec position, destination; uint8_t pierce{}; };
struct ItemTargetingFact { EntityId source; int cursor{-1}; int skill{-1}; };
struct ItemFact { TransactionId transaction; ItemChange change; };
// Sparse immutable projection captured at commit. Encoding never consults a
// later live inventory: several commands can commit before output is drained.
struct InventoryFact {
    PersistentCharacter projection;
    std::vector<ItemChange> changes;
    bool switchedWeapons{};
};
struct QuestFact { PlayerId player; CharacterRecord record; int difficulty{}; unsigned remaining{}; std::array<int,5> stones{}; };
struct AttributeFact { EntityId unit; uint64_t revision{}; };
struct CharacterFact {
    CharacterRecord before, after;
    attributes::Totals previous, current;
    std::optional<bool> selectedHand{}; // Explicit selection also confirms an unchanged value.
};
struct TravelFact { PlayerId player; EntityId actor; RegionId from, to; uint64_t areaGeneration{}; Vec position; bool walking{}, revived{}; };
struct ObjectFact { EntityId object, actor; RegionId area; uint64_t revision{}; };
struct ChatFact { EntityId actor; std::string name, text; std::vector<PlayerId> recipients; };
struct CommandFact { PlayerId player; uint64_t sequence{}; CommandStatus result; };
struct NpcMessage { uint8_t menu{}; uint16_t text{}; };
struct NpcMessagesFact { EntityId npc; std::vector<NpcMessage> messages; };
struct MerchantFact { EntityId npc, item; uint8_t operation{}, result{}; uint32_t gold{}; bool refreshShop{}; };
struct WaypointFact { EntityId source; std::vector<RegionId> unlocked; };
struct UiFact { uint8_t action{}; };
struct NpcServiceFact { EntityId npc; uint8_t result{}; };
// Captured at the drop transaction; later visibility snapshots are not drops.
struct GroundDropFact { ItemInstance item; };
struct GroundRemoveFact { EntityId item; };
struct GroundRestoredFact {EntityId item;uint64_t revision{};};
using DomainFact = std::variant<ManaFact, RepositionFact, LifeFact, AttackFact, HitFact, DeathFact, ItemFact, InventoryFact, CharacterFact, QuestFact, AttributeFact, TravelFact, ObjectFact, ChatFact, CommandFact, NpcMessagesFact, MerchantFact, UiFact, WaypointFact, StateFact, MissileFact, SkillPulseFact, SoundFact, OverlayFact, ItemTargetingFact, GroundDropFact, NpcServiceFact, GroundRemoveFact, ItemSkillFact, GroundRestoredFact>;
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
