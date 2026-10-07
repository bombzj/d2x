#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/handle.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::trade {
// Two-player offers reference items; final exchange uses one atomic transaction.
enum class Action { Invite, Accept, Cancel, Agree, Revoke, OfferGold };
struct Request { Action action; std::optional<PlayerId> peer; uint64_t revision{}; unsigned gold{}; };
struct Offer { PlayerId player; unsigned gold{}; std::vector<ItemHandle> items; bool agreed{}; };
struct Exchange { TransactionId transaction; Offer first, second; uint64_t revision{}; };
struct State { std::map<TransactionId, Exchange> exchanges; };
struct Ports { const PlayerStore &players; const items::System &items; transactions::System &transactions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> cancelFor(PlayerId);
};
}
