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
struct Controller { EntityId actor; std::optional<EntityId> target; uint64_t nextDecision{}; int behavior{}; uint64_t random{}; bool pursuing{}, charged{}; uint64_t nextPath{}; };
struct State { std::map<EntityId, Controller> controllers; };
struct Ports { monsters::System &monsters; const PlayerStore &players; const AreaStore &areas; skills::System &skills; uint64_t &random; };
class System {
    State state_;
    const Ports ports_;
    StepStatus meleeFamily(EntityId, UnitTarget, Vec, int, TickContext, Controller &);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    StepStatus step(TickContext, FrameFacts &);
};
}
