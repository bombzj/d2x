#pragma once
#include "contracts/online.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
// Explicit caller policy for single items (not stacks); an empty list permits
// only lossless arrangements. The requested storage item is always retained.
struct InventoryCleanupPolicy {std::vector<std::string> discardableCodes;};
// A read-only packing query returning a complete sequence of ordinary item
// operations. Discard is considered only after lossless planning fails.
struct BackpackArrangement {
    enum class Result {Ready, UnknownLayout, InsufficientArea, SearchLimit, NoPlan} result=Result::UnknownLayout;
    std::vector<OnlineItemCommand> commands;
    size_t searched{},freeCells{};
};
BackpackArrangement planBackpackArrangement(const OnlineView &,const OnlineInventoryView &,uint32_t item,
    const InventoryCleanupPolicy &policy={});

// Input-independent client job. The caller submits next() through the normal
// inventory function, then passes the new normal-client submission receipt.
// Only observed item/storage postconditions advance an accepted operation.
class InventoryWorkflow {
public:
    using Clock=std::chrono::steady_clock;
    enum class Kind {Store, Combine};
    enum class State {Idle, Running, Done, Failed};
    void store(uint32_t item,const OnlineView &,InventoryCleanupPolicy policy={});
    void combine(std::string container,std::string output,std::vector<std::string> ingredients,const OnlineView &,InventoryCleanupPolicy policy={});
    void observe(const OnlineView &);
    std::optional<OnlineItemCommand> next(const OnlineView &,const OnlineInventoryView &);
    void submitted(const std::optional<OnlineItemRequest> &receipt,uint64_t previousSequence);
    bool active() const {return state_==State::Running;}
    bool started() const {return state_!=State::Idle;}
    bool done() const {return state_==State::Done;}
    State state() const {return state_;}
    Kind kind() const {return kind_;}
    const std::string &reason() const {return reason_;}
    unsigned completedOperations() const {return completed_;}
    size_t remainingPlacements() const {return arrangement_.size();}
    Clock::time_point deadline() const {return pending_?pending_->deadline:Clock::time_point{};}
private:
    struct Pending {OnlineItemCommand command;uint64_t sequence{};Clock::time_point deadline;bool placement{};};
    Kind kind_=Kind::Store;State state_=State::Idle;
    uint64_t game_{},connection_{};std::optional<uint32_t> player_;
    uint32_t item_{},arrangingItem_{};std::string container_,output_,reason_;
    std::vector<std::string> ingredients_;
    std::vector<uint32_t> consumed_;
    InventoryCleanupPolicy cleanup_;
    std::deque<OnlineItemCommand> arrangement_;
    std::optional<OnlineItemCommand> proposed_;
    std::optional<Pending> pending_;
    bool proposedPlacement_{};unsigned completed_{};
    void begin(const OnlineView &);
    [[noreturn]] void fail(std::string);
    void arrange(const OnlineView &,const OnlineInventoryView &,uint32_t);
};
} // namespace d2x
