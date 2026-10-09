#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/npc_rules.hpp"
namespace d2x::server::merchant {
enum class Action { Open, Gamble, Buy, Sell, Repair, RepairAll, IdentifyAll };
struct Request { Action action; UnitTarget npc; std::optional<ItemHandle> item; uint64_t offerRevision{}; bool gamble{}, multibuy{}; };
struct Offer { ItemInstance item; EquipmentValues equipment; bool permanent{}; std::string displayCode; };
struct Stock { uint64_t revision = 1; std::map<EntityId, Offer> offers; RegionId area{}; EquipmentRules setRules; bool generated{}; uint64_t conversation{}, refreshAt{}; bool refreshPending{}; };
struct Pending { ActorContext actor; Request request; uint64_t token{}, seed{}, conversation{}; };
struct ShopSession { EntityId npc; uint64_t conversation{}; };
struct Preparation {
    Pending pending; NpcRule npc; PersistentCharacter character; int reducedPrices{}, difficulty{};
    uint64_t inventoryRevision{}, characterRevision{}; Stock stock; uint64_t seed{};
    std::set<size_t> uniques;
};
struct Prepared {
    Preparation source; std::vector<Offer> offers; std::map<EntityId,unsigned> prices, unitPrices;
    std::string deferred;
    std::map<EntityId,ItemInstance> repairs;
    std::shared_ptr<const EquipmentRules> equipment;
    std::set<size_t> limitedUniques;
};
struct State { std::map<EntityId,Stock> stocks; std::map<std::pair<PlayerId,EntityId>,Stock> gambles; std::map<PlayerId,Pending> pending; std::map<PlayerId,ShopSession> opened; std::set<PlayerId> gambling; uint64_t next = 1; std::string deferred; };
struct Ports { const PlayerStore &players; const npc::System &npc; items::System &items; transactions::System &transactions; EventOutbox &events; uint64_t &random; const ItemCatalog *definitions; const GameSettings &settings; loot::System &loot; };
class System {
    State state_; const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &,const Request &);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::optional<PersistentCharacter> shop(PlayerId) const;
    StepStatus step(TickContext, FrameFacts &);
};
}
