#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "server/systems/monsters/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::population {
// Prepared spawn identities and per-area population lifecycle; no implicit Fallen fallback.
using Spawn = monsters::Admission;
struct AreaPopulation { uint64_t generation{}; std::set<std::string> admittedSpawnKeys; };
struct State { std::map<RegionId, AreaPopulation> areas; };
struct Ports { const AreaStore &areas; monsters::System &monsters; uint64_t &random; const GameSettings &settings; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Spawn &);
    StepStatus step(TickContext, FrameFacts &);
};
}
