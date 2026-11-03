#pragma once
#include "event_batch.hpp"
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace d2x::server {
struct DiagnosticEvent;
// Reliable authority-to-host output. Payloads, storage and diagnostics have
// independent headers; queue implementation changes stay in events.cpp.
class EventOutbox {
    struct Storage;
    std::unique_ptr<Storage> storage_;
    void observe(const EventBatch &) noexcept;
    DomainResult<uint64_t> publishReadyGroup(std::vector<EventBatch>);
  public:
    EventOutbox();
    ~EventOutbox();
    EventOutbox(const EventOutbox &) = delete;
    EventOutbox &operator=(const EventOutbox &) = delete;
    bool hasCapacity(size_t count, size_t batches = 1) const;
    // Attached sounds share the native per-unit update slot. Sequence zero
    // means its state was accepted; flushUnitSounds publishes the final wire fact.
    DomainResult<uint64_t> publish(EventBatch);
    DomainResult<uint64_t> publishGroup(std::vector<EventBatch>);
    DomainResult<uint64_t> flushUnitSounds(uint64_t tick,const std::function<std::optional<RegionId>(const SoundFact &)> &);
    bool hasStagedSounds() const;
    void discardUnitSound(EntityId actor) noexcept;
    const std::deque<EventBatch> &pending() const;
    bool acknowledge(uint64_t sequence);
    bool hasPendingSound(EntityId) const;
    uint64_t historyFirst() const;
    uint64_t historyLast() const;
    std::vector<DiagnosticEvent> history(uint64_t since, size_t limit) const;
};
}
