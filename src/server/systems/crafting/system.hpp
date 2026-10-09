#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/intents.hpp"
#include "gameplay/npc/intents.hpp"
#include "server/systems/travel/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::crafting {
struct RewardItem { EntityId npc; ItemHandle item; };
using Intent = std::variant<TransmuteCube, SocketItem, ImbueItem, SocketQuestItem, PersonalizeQuestItem, RewardItem>;
struct Request { Intent intent; };
struct Pending { ActorContext actor; Request request; uint64_t token{}, seed{}, inventoryRevision{}, characterRevision{}, conversation{}; };
// Immutable content-worker input. Authority revalidates revisions and access
// before placing every output and consuming every input in one transaction.
struct Preparation {
    Pending pending; PersistentCharacter character; std::string classCode;
    uint64_t inventoryRevision{}, characterRevision{}; int difficulty{}; bool storage{}, cube{};
    std::string npc;
};
struct Output { ItemInstance item; std::optional<ItemHandle> replacement; EntityId container; };
struct Prepared {
    Preparation source; std::vector<ItemHandle> consumed; std::vector<Output> outputs;
    std::shared_ptr<const EquipmentRules> equipment; std::string deferred;
    std::optional<CharacterRecord> character;
    std::optional<travel::SpecialPortalKind> portal;
};
struct State { std::map<PlayerId, Pending> pending; uint64_t next = 1; std::string deferred; };
struct Ports {
    const PlayerStore &players; const AreaStore &areas; items::System &items; transactions::System &transactions;
    const ItemCatalog *definitions; const inventory::System &inventory;
    const GameSettings &settings; uint64_t &random; const npc::System &npc; travel::System &travel; EventOutbox &events;
};
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    StepStatus step(TickContext, FrameFacts &);
};
}
