#pragma once
#include "calculation.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include <utility>
#include <vector>
namespace d2x::server::attributes {
// Totals are evaluated eagerly before admission/transaction publication.
// No dirty queue and no second tick can observe stale equipment or level values.
struct State {
    std::optional<std::pair<int,uint64_t>> environment;
    std::vector<PlayerId> players;
    size_t next{};
};
struct Ports {const PlayerStore &players;const AreaStore &areas;transactions::System &transactions;world::System &world;};
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<Totals> evaluate(EntityId) const;
    StepStatus refreshEnvironment(TickContext);
    StepStatus step(TickContext, FrameFacts &);
};
}
