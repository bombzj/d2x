#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::social {
// Player relationships, party membership and chat routing; no wire encoding.
enum class Action { Chat, Invite, Accept, LeaveParty, Hostility };
struct Request { Action action; std::optional<PlayerId> target; std::string text; };
struct Relation { bool hostile{}; uint64_t revision{}; };
struct State { std::map<PlayerId, uint64_t> parties; std::map<std::pair<PlayerId, PlayerId>, Relation> relations; };
struct Ports { const PlayerStore &players; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
};
}
