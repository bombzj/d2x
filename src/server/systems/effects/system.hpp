#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::effects {
// Timed states, auras and curses; derived stats are invalidated, never duplicated.
struct Apply { EntityId source, target; int state{}; uint64_t durationFrames{}; unsigned level{}; };
struct Effect { EntityId id, source, target; int state{}; uint64_t expires{}, revision{}; };
struct State { std::map<EntityId, Effect> effects; };
struct Ports { const PlayerStore &players; const monsters::System &monsters; const spatial::System &spatial; transactions::System &transactions; EntityIds &ids; const EffectRules *definitions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> apply(const Apply &);
    StepStatus step(TickContext, FrameFacts &);
};
}
