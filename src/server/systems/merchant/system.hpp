#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/handle.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::merchant {
// Vendor stocks reference canonical items; price/repair/gamble settlement is server-owned.
enum class Action { Open, Gamble, Buy, Sell, Repair, RepairAll, IdentifyAll };
struct Request { Action action; UnitTarget npc; std::optional<ItemHandle> item; uint64_t offerRevision{}; };
struct Stock { uint64_t revision{}; std::vector<ItemHandle> items; };
struct State { std::map<EntityId, Stock> stocks; };
struct Ports { const PlayerStore &players; const npc::System &npc; const items::System &items; transactions::System &transactions; uint64_t &random; const ItemCatalog *definitions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
};
}
