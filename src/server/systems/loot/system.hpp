#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/plan.hpp"
#include "gameplay/loot/chest.hpp"
#include "server/systems/items/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::loot {
// Treasure selection only; a selected plan is not materialized or committed loot.
struct ObjectSource { int definition{}, operation{}; std::optional<ChestState> chest; ItemHandle key; };
struct Request { uint64_t occurrence{}; LootRequest source; PlayerId beneficiary; Vec position; std::optional<ObjectSource> object{}; };
struct Preparation { Request request; PersistentCharacter character; std::string classCode; uint64_t seed{}; int magicFind{}, goldFind{}; std::set<size_t> uniques; };
struct State { std::map<EntityId, Preparation> pending; std::set<size_t> uniques; std::string deferred; std::map<EntityId, bool> completed; };
struct Ports { const PlayerStore &players; const quests::System &quests; items::System &items; transactions::System &transactions; uint64_t &random; const TreasureRules *definitions; const GameSettings &settings; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> queue(const Request &);
    std::optional<bool> takeCompletion(EntityId);
    DomainResult<> install(EntityId source, items::PreparedBatch, std::string deferred);
    StepStatus step(TickContext, FrameFacts &);
};
}
