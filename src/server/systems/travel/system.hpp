#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "gameplay/items/handle.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::travel {
// Travel/waypoint/portal transition preparation; destination content arrives through World.
enum class Kind { Exit, Waypoint, Portal, Npc, SpecialPortal };
enum class SpecialPortalKind { Cow, Pandemonium, Finale };
struct Request { Kind kind; UnitTarget source; std::optional<RegionId> destination; };
struct Transition { RegionId from, to; uint64_t request{}, sourceGeneration{}, sequence{}; EntityId source; Vec approach, arrival, destination; bool walking{}, run{}, crossing{}; int side{}, plane{}; Kind kind=Kind::Exit; };
struct Portal { PlayerId player; EntityId owner,fieldId,townId; std::string name; RegionId field,town; Vec fieldPosition,townPosition; PortalRule rule; uint64_t ready{},revision=1; bool opened{}; bool shared{}; };
struct WaypointAccess { EntityId source; RegionId area; uint64_t generation{}; std::optional<int> remoteRange{}; };
struct State { std::map<PlayerId, Transition> transitions; std::map<PlayerId,Portal> portals; std::map<PlayerId,WaypointAccess> waypoints; std::map<RegionId,Portal> specialPortals; };
struct SpecialPortalPlan {std::map<RegionId,Portal> next;};
struct Ports { PlayerStore &players; const AreaStore &areas; world::System &world; trade::System &trade; npc::System &npc; transactions::System &transactions; EventOutbox &events; inventory::System &inventory; items::System &items; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> openWaypoint(const ActorContext &,EntityId,std::optional<int> remoteRange = {});
    DomainResult<> createPortal(const ActorContext &,std::optional<ItemHandle> = {},std::optional<int> skill = {});
    DomainResult<SpecialPortalPlan> prepareSpecialPortal(const ActorContext &,SpecialPortalKind,uint64_t seed);
    void commitSpecialPortal(SpecialPortalPlan plan) noexcept {state_.specialPortals.swap(plan.next);}
    std::optional<Vec> portalPosition(const ActorContext &,EntityId) const;
    DomainResult<> useSpecial(const ActorContext &,const Request &,std::optional<int> remoteRange = {});
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    std::optional<CommandStatus> walk(const ActorContext &, const MovementCommand &);
    DomainResult<> teleport(const ActorContext &, PointTarget, float manaCost,std::optional<SkillCharge> charge = {});
    void cancel(PlayerId id) { state_.transitions.erase(id); }
};
}
