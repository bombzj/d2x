#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
struct Request { UnitTarget target; };
struct Admission { int definition{}; RegionId area; Vec position; };
struct Object { EntityId id; int definition{}; RegionId area; Vec position; uint64_t revision{}; int mode{}; };
struct State { std::map<EntityId, Object> objects; };
struct Ports { const PlayerStore &players; const AreaStore &areas; quests::System &quests; loot::System &loot; effects::System &effects; transactions::System &transactions; EntityIds &ids; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
