#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::missiles {
// Server missile instances and collision; client visual missiles are not authority.
struct Spawn { EntityId owner; int definition{}; PointTarget origin; ActionTarget target; unsigned skillLevel{}; };
struct Missile { EntityId id, owner; int definition{}; RegionId area; Vec position; uint64_t revision{}, expires{}; };
struct State { std::map<EntityId, Missile> missiles; };
struct Ports { const AreaStore &areas; const spatial::System &spatial; combat::System &combat; EntityIds &ids; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> spawn(const Spawn &);
    StepStatus step(TickContext, FrameFacts &);
};
}
