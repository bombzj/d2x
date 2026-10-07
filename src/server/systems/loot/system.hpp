#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/plan.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::loot {
// Treasure selection only; a selected plan is not materialized or committed loot.
struct Request { uint64_t occurrence{}; LootRequest source; PlayerId beneficiary; Vec position; };
struct State { std::set<uint64_t> committedOccurrences; };
struct Ports { const PlayerStore &players; const quests::System &quests; items::System &items; transactions::System &transactions; uint64_t &random; const TreasureRules *definitions; const GameSettings &settings; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<LootPlan> plan(const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
