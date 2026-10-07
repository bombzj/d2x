#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/npc/intents.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::npc {
// Conversation sessions and quest dialogue qualification; merchants own shop transactions.
// Shop/crafting/hireling/travel requests route directly to their own domains.
using Intent = std::variant<TalkToNpc, EndNpcConversation>;
struct Request { Intent intent; };
struct Conversation { EntityId npc; uint64_t revision{}; std::optional<uint32_t> pendingMessage; };
struct State { std::map<PlayerId, Conversation> conversations; };
struct Ports { const PlayerStore &players; const monsters::System &monsters; const quests::System &quests; const spatial::System &spatial; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> close(PlayerId);
};
}
