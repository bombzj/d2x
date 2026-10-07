#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/character/attributes.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::attributes {
// Derived equipment, passive and resource totals; invalidation owns no base stats.
struct State { std::set<EntityId> dirty; };
struct Totals { CharacterAttributes character; uint64_t sourceRevision{}; };
struct Ports { const PlayerStore &players; const items::System &items; const effects::System &effects; const companions::System &companions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<Totals> evaluate(EntityId) const;
    StepStatus step(TickContext, FrameFacts &);
};
}
