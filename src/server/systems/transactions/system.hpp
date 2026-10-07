#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::transactions {
// Only cross-domain commit boundary; reservation/validation/commit must be atomic.
struct ItemTransfer { std::optional<PlayerId> from, to; ItemHandle item; ItemDestination destination; };
struct Reward { PlayerId player; uint64_t sourceOccurrence{}, experience{}; unsigned gold{}; };
struct Exchange { PlayerId first, second; std::vector<ItemTransfer> items; unsigned firstGold{}, secondGold{}; };
using Change = std::variant<ItemTransfer, Reward, Exchange>;
struct Plan { TransactionId id; std::vector<RevisionGuard> expected; Change change; };
struct State { std::set<TransactionId> committed; };
struct Ports { PlayerStore &players; items::System &items; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<Plan> prepare(const Change &) const;
    DomainResult<> commit(Plan);
};
}
