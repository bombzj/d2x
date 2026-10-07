#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/intents.hpp"
#include "gameplay/npc/intents.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::crafting {
// Cube, socket, imbue and personalization transaction plans.
using Intent = std::variant<TransmuteCube, SocketItem, ImbueItem, SocketQuestItem, PersonalizeQuestItem>;
struct Request { Intent intent; };
struct State { std::map<PlayerId, TransactionId> pending; };
struct Ports { const PlayerStore &players; const items::System &items; transactions::System &transactions; const ItemCatalog *definitions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
};
}
