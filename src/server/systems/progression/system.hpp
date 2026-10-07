#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/character/intents.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::progression {
// Experience, level, allocated stats/skills and quest rewards; no client-side grants.
using Intent = std::variant<AllocateAttribute, AllocateSkill>;
struct Request { Intent intent; unsigned count = 1; };
struct Award { PlayerId player; uint64_t sourceOccurrence{}, experience{}; };
struct State { std::set<uint64_t> committedAwards; };
struct Ports { const PlayerStore &players; const social::System &relations; transactions::System &transactions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> award(const Award &);
    StepStatus step(TickContext, FrameFacts &);
};
}
