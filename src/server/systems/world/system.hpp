#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::world {
// Area activation and prepared-area handoff; generation stays in the host.
enum class Residency { Prepared, Active, Sleeping };
struct AreaLease { RegionId area; uint64_t generation{}; Residency residency; };
struct PrepareArea { RegionId destination; uint64_t request{}; GameSettings settings; };
struct PreparedArea { uint64_t request{}; AreaDefinition definition; };
struct State { std::map<RegionId, AreaLease> residency; std::vector<PrepareArea> preparation; };
struct Ports { AreaStore &areas; const PlayerStore &players; const GameSettings &settings; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<uint64_t> requestArea(const ActorContext &, RegionId);
    DomainResult<> install(PreparedArea);
    StepStatus step(TickContext, FrameFacts &);
};
}
