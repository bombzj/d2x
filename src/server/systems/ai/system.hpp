#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::ai {
// Decision state only; actions go through skill/movement requests, not direct damage.
struct Controller { EntityId actor; std::optional<EntityId> target; uint64_t nextDecision{}; int behavior{}; };
struct State { std::map<EntityId, Controller> controllers; };
struct Ports { monsters::System &monsters; const spatial::System &spatial; const social::System &relations; skills::System &skills; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    StepStatus step(TickContext, FrameFacts &);
};
}
