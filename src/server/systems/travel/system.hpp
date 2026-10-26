#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "gameplay/items/handle.hpp"

#include <map>
#include <array>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::travel {
// Travel/waypoint/portal transition preparation; destination content arrives through World.
enum class Kind { Exit, Waypoint, Portal, Npc, SpecialPortal, QuestObject, NativeObjectPortal };
enum class SpecialPortalKind { Cow, Pandemonium, Finale, Tristram, CainRescue, Andariel, ArcaneCanyon, Tyrael, ActFive, AnyaTemple, BaalExit, Shrine };
struct Request { Kind kind; UnitTarget source; std::optional<RegionId> destination; };
struct Transition { RegionId from, to; uint64_t request{}, sourceGeneration{}, sequence{}; EntityId source; Vec approach, arrival; Kind kind=Kind::Exit; uint64_t npcConversation{}, waypointRevision{}; std::optional<uint64_t> arrivalGeneration{}; };
struct Portal { PlayerId player; EntityId owner,fieldId,townId; std::string name; RegionId field,town; Vec fieldPosition,townPosition; PortalRule rule; uint64_t ready{},revision=1; bool opened{}; bool shared{};std::optional<RoomView> fieldRoom{},townRoom{}; std::optional<bool> linkedOpened{};
    bool isOpened(bool linked) const {return linked?linkedOpened.value_or(opened):opened;}
};
struct WaypointAccess { EntityId source; RegionId area; uint64_t generation{}, revision{}; };
struct PositionSynchronization { uint64_t until{}; std::array<uint64_t,5> attempts{}; };
struct State { std::map<PlayerId, Transition> transitions; std::map<PlayerId,Portal> portals; std::map<PlayerId,WaypointAccess> waypoints; std::map<EntityId,Portal> specialPortals; std::map<PlayerId,PositionSynchronization> synchronizations; std::shared_ptr<const pd2::MapInstance> pd2Map; EntityId pd2MapPortal;std::map<PlayerId,EntityId> mapReports;std::optional<ActorContext> pendingPd2Map; };
struct SpecialPortalPlan {std::map<EntityId,Portal> next;};
struct Ports { PlayerStore &players; AreaStore &areas; world::System &world; trade::System &trade; npc::System &npc; transactions::System &transactions; EventOutbox &events; inventory::System &inventory; items::System &items; objects::System &objects; quests::System &quests; const social::System &social; uint64_t &random; const GameSettings &settings; const spatial::System &spatial; };
class System {
    State state_;
    const Ports ports_;
    uint64_t nextWaypointRevision_=1;
    void writePortalCollision(const Portal &,bool) noexcept;
    struct NativeLinkPosition {RegionId area;uint64_t generation{};Vec position;std::optional<RoomView> room;};
    std::map<EntityId,NativeLinkPosition> nativeLinks_;
    std::optional<Portal> pd2OpeningPortal_;
    bool pd2Cancelling_{};
    const AreaObject *waypointAccess(const ActorContext &,EntityId,std::optional<uint64_t> revision = {}) const;
    bool waypointBusy(PlayerId) const;
    StepStatus expireSynchronizations(TickContext);
    StepStatus synchronizePd2MapReports(TickContext);
    DomainResult<> finishPd2Map(const ActorContext &,ItemHandle);
    void advancePd2Map(TickContext);
    void discardPd2MapPreparation();
    DomainResult<SpecialPortalPlan> prepareWorldPortal(RegionId,int,uint64_t,Vec,const PortalRule &,bool blue,bool exactSource,bool durielOpening=false);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> openWaypoint(const ActorContext &,EntityId,bool menu,std::optional<int> remoteRange = {});
    DomainResult<> unlockWaypoints(const ActorContext &,std::optional<RegionId>); // Local host administration; absent destination selects all.
    DomainResult<> createPortal(const ActorContext &,std::optional<ItemHandle> = {},std::optional<int> skill = {});
    DomainResult<> createPd2Map(const ActorContext &,ItemHandle);
    DomainResult<SpecialPortalPlan> prepareSpecialPortal(const ActorContext &,SpecialPortalKind,uint64_t seed,std::optional<Vec> source = {});
    DomainResult<SpecialPortalPlan> prepareNativePermanentPortal(RegionId,Vec,int destination,uint64_t tick,int definition=60);
    DomainResult<> linkNativePortal(EntityId source,uint64_t tick);
    DomainResult<> useNativeObjectPortal(const ActorContext &,EntityId,std::optional<int> remoteRange={});
    void commitSpecialPortal(SpecialPortalPlan) noexcept;
    std::optional<Vec> portalPosition(const ActorContext &,EntityId) const;
    std::optional<std::pair<RegionId,Vec>> nativePortalPosition(EntityId) const;
    DomainResult<> useSpecial(const ActorContext &,const Request &,std::optional<int> remoteRange = {});
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> walked(const ActorContext &);
    DomainResult<> synchronize(const ActorContext &, Vec, bool uninterruptible);
    bool synchronizationState(PlayerId id) const {
        const auto found=state_.synchronizations.find(id);
        return found!=state_.synchronizations.end() && found->second.until!=0;
    }
    DomainResult<> teleport(const ActorContext &, PointTarget, Fixed8 manaCost,std::optional<SkillCharge> charge = {},int skill=-1);
    DomainResult<> castTeleport(const ActorContext &, PointTarget, Fixed8 manaCost,std::optional<SkillCharge> charge = {},int skill=-1);
    DomainResult<Vec> pd2TeleportLanding(RegionId,Vec source,Vec target,int size) const;
    DomainResult<> relocate(const ActorContext &,RegionId,std::optional<Vec>); // Local host administration, never a wire command.
    void cancel(PlayerId id) { state_.transitions.erase(id); }
    void closePlayerPortal(PlayerId);
    void closePortals(RegionId);
};
}
