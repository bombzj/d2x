#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::travel {
// Travel/waypoint/portal transition preparation; destination content arrives through World.
enum class Kind { Exit, Waypoint, Portal, Npc };
struct Request { Kind kind; UnitTarget source; std::optional<RegionId> destination; };
struct Transition { RegionId from, to; uint64_t request{}, sourceGeneration{}, sequence{}; EntityId source; Vec approach, arrival, destination; bool walking{}, run{}, crossing{}; int side{}, plane{}; };
struct State { std::map<PlayerId, Transition> transitions; };
struct Ports { PlayerStore &players; const AreaStore &areas; world::System &world; trade::System &trade; npc::System &npc; transactions::System &transactions; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    std::optional<CommandStatus> walk(const ActorContext &, const MovementCommand &);
    DomainResult<> teleport(const ActorContext &, PointTarget, float manaCost);
    void cancel(PlayerId id) { state_.transitions.erase(id); }
};
}
