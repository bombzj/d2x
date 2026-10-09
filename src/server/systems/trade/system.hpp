#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/handle.hpp"
#include "server/systems/transactions/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::trade {
// Two-player offers reference items; final exchange uses one atomic transaction.
enum class Action { Invite, Accept, Cancel, Agree, Revoke, OfferGold };
struct Request { Action action; std::optional<PlayerId> peer; uint64_t revision{}; unsigned gold{}; };
struct Offer {
    PlayerId player; unsigned gold{}; bool agreed{};
    EntityId container{};
    InventoryState original;
    std::shared_ptr<const EquipmentRules> equipment;
    std::map<EntityId,EntityId> mirrors;
};
struct Exchange { TransactionId transaction; Offer first, second; uint64_t revision{1}; bool open{}; uint64_t unlock{}; };
struct State { std::map<TransactionId, Exchange> exchanges; uint64_t next{1}; std::map<PlayerId,uint64_t> cooldown; };
struct Ports {
    const PlayerStore &players; items::System &items; transactions::System &transactions;
    const AreaStore &areas; EventOutbox &events; inventory::System &inventory; npc::System &npc;
};
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    const Exchange *find(PlayerId) const;
    EntityId container(PlayerId) const;
    bool canEdit(PlayerId) const;
    // Attach immutable peer mirrors/reset facts before the inventory commit.
    DomainResult<> decorate(PlayerId, transactions::InventoryEdit &);
    void edited(PlayerId, uint64_t tick) noexcept;
    void normalize(PlayerId, PersistentCharacter &) const;
    StepStatus step(TickContext);
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> cancelFor(PlayerId, uint64_t tick = 0);
  private:
    DomainResult<> complete(Exchange &, uint64_t tick);
};
}
