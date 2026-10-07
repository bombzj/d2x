#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/damage_type.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::combat {
// Hit, mitigation and resource damage planning; death is a separate transition.
struct Damage { EntityId source, target; DamageType type; int64_t minimum{}, maximum{}; uint64_t action{}; };
struct State { std::vector<Damage> pending; };
struct Ports { const PlayerStore &players; const monsters::System &monsters; const attributes::System &attributes; const social::System &relations; transactions::System &transactions; uint64_t &random; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> enqueue(const Damage &);
    StepStatus step(TickContext, FrameFacts &);
};
}
