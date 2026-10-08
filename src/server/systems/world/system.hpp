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
struct Ports { AreaStore &areas; const PlayerStore &players; const GameSettings &settings; EntityIds &ids; };
class System {
    State state_;
    const Ports ports_;
    uint64_t nextRequest_ = 1;
    std::map<RegionId, uint64_t> failed_;
    DomainResult<uint64_t> request(RegionId);
    void link();
    void assign(AreaDefinition &);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<uint64_t> requestWaypoint(const ActorContext &,RegionId);
    DomainResult<uint64_t> requestTown(const ActorContext &);
    DomainResult<uint64_t> requestArea(const ActorContext &, RegionId);
    DomainResult<> install(PreparedArea);
    void initialize();
    void objectCollision(RegionId, std::vector<Grid::Obstacle>);
    void fail(uint64_t request);
    std::vector<RegionId> visible(PlayerId) const;
    StepStatus step(TickContext, FrameFacts &);
};
}
