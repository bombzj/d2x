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
enum class Action { Chat, Invite, CancelInvite, Accept, LeaveParty, Hostility, Ignore, Squelch };
struct Request { Action action; std::optional<PlayerId> target; std::string text; std::string receiver{}; bool overhead{}; uint8_t language{}; bool enabled{}; };
struct Relation { bool hostile{}; uint64_t revision{}; uint16_t flags{}; };
struct Hover { std::string text; uint64_t expires{},revision{}; };
struct State { std::map<PlayerId, uint16_t> parties; std::set<std::pair<PlayerId,PlayerId>> invitations; uint16_t nextParty{3}; std::map<std::pair<PlayerId, PlayerId>, Relation> relations; std::map<PlayerId,Hover> hover; uint64_t nextHover{1}; };
struct Ports { const PlayerStore &players; EventOutbox &events; const trade::System &trade; };
class System {
    State state_;
    const Ports ports_;
    DomainResult<> party(const ActorContext &, const Request &);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    uint16_t partyId(PlayerId) const;
    bool sameParty(PlayerId,PlayerId) const;
    uint8_t partyStatus(PlayerId observer,PlayerId target) const;
    uint16_t flags(PlayerId from,PlayerId to) const;
    DomainResult<> disconnect(PlayerId,uint64_t tick);
    DomainResult<> execute(const ActorContext &, const Request &);
    std::optional<PlayerSnapshot::Hover> overhead(PlayerId, uint64_t tick) const;
    void step(TickContext);
};
}
