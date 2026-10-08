#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::replication {
// Recipient interest and ordered domain projection; native serialization stays in hosting.
struct Interest { RegionId area; uint64_t generation{}; std::set<EntityId> visible; };
struct State { std::map<PlayerId, Interest> recipients; };
struct Ports { const PlayerStore &players; const AreaStore &areas; const spatial::System &spatial; const monsters::System &monsters; EventOutbox &events; const effects::System &effects; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> admit(PlayerId);
    std::vector<PlayerId> visible(PlayerId) const;
    std::vector<MonsterSnapshot> visibleMonsters(PlayerId, uint64_t tick) const;
    std::vector<PetOwnershipSnapshot> pets(PlayerId) const;
};
}
