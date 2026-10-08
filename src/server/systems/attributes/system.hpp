#pragma once
#include "calculation.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
namespace d2x::server::attributes {
// Totals are evaluated eagerly before admission/transaction publication.
// No dirty queue and no second tick can observe stale equipment or level values.
struct State {};
struct Ports { const PlayerStore &players; };
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
