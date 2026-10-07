#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/generation.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::items {
// Canonical non-character items; character-owned items remain in PlayerStore.
struct CreateItem { std::string code; unsigned level{}; ItemGeneration generation; ItemLocation location; };
struct State { InventoryState world; };
struct Address { std::optional<PlayerId> character; ItemHandle item; };
struct Ports { const PlayerStore &players; EntityIds &ids; uint64_t &random; const ItemCatalog *definitions; };
class System {
    friend class transactions::System;
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<ItemInstance> create(const CreateItem &);
    DomainResult<ItemInstance> resolve(const Address &) const;
};
}
