#pragma once
#include "contracts.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/quest/id.hpp"
#include <deque>
#include <span>
#include <vector>

namespace d2x::server {
struct DeathFact { uint64_t occurrence{}; EntityId victim, killer; RegionId area; };
struct ItemFact { TransactionId transaction; ItemChange change; };
struct QuestFact { PlayerId player; QuestId quest; uint64_t revision{}; };
struct AttributeFact { EntityId unit; uint64_t revision{}; };
struct TravelFact { PlayerId player; RegionId from, to; uint64_t areaGeneration{}; };
struct ObjectFact { EntityId object, actor; RegionId area; uint64_t revision{}; };
struct CommandFact { PlayerId player; uint64_t sequence{}; CommandStatus result; };
using DomainFact = std::variant<DeathFact, ItemFact, QuestFact, AttributeFact, TravelFact, ObjectFact, CommandFact>;
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
  public:
    bool hasCapacity(size_t count) const;
    DomainResult<uint64_t> publish(EventBatch);
    const std::deque<EventBatch> &pending() const { return batches_; }
    bool acknowledge(uint64_t sequence);
};
}
