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
struct State { InventoryState world; EquipmentRules equipment; uint64_t revision = 1; };
struct PreparedBatch { std::vector<ItemInstance> items; std::shared_ptr<const EquipmentRules> equipment; std::set<size_t> limitedUniques; };
struct Address { std::optional<PlayerId> character; ItemHandle item; };
struct Ports { const PlayerStore &players; const AreaStore &areas; EntityIds &ids; uint64_t &random; const ItemCatalog *definitions; };
class System {
    friend class transactions::System;
    friend class loot::System;
    void commit(State next) noexcept { std::swap(state_, next); }
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    bool identityCapacity(size_t count) const { return ports_.ids.cursor()<=UINT32_MAX && count<=uint64_t(UINT32_MAX)-ports_.ids.cursor()+1; }
    EntityId reserveIdentity() { if (ports_.ids.cursor() > UINT32_MAX) throw std::overflow_error("Native item identity exhausted"); return ports_.ids.allocate(); }
    DomainResult<State> prepare(PreparedBatch, GroundLocation);
    DomainResult<> install(PreparedBatch, GroundLocation);
    bool reachable(GroundLocation, Vec from) const;
    std::optional<Vec> placement(GroundLocation, const InventoryState &) const;
    DomainResult<ItemInstance> resolve(const Address &) const;
};
}
