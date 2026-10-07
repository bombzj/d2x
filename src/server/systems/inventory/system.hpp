#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/intents.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::inventory {
// Inventory access and placement plans; no second occupancy or item copy.
using Intent = std::variant<MoveItem, TransferItem, SwapItems, SplitStack, MergeStacks,
    LoadBook, IdentifyItem, EquipBelt, EquipItem, EquipHirelingItem, UseItem,
    UseBeltColumn, UseHirelingPotion, SwitchWeaponSet, CloseStorage, GoldTransaction>;
struct Request { Intent intent; };
struct Access { EntityId source; ContainerKind kind; uint64_t revision{}; };
struct State { std::map<PlayerId, Access> storage; };
struct Ports { const PlayerStore &players; const items::System &items; transactions::System &transactions; const ItemCatalog *definitions; };
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
