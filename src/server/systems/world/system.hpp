#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"

#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace d2x::server::world {
// Area activation and prepared-area handoff; generation stays in the host.
enum class Residency { Prepared, Active, Sleeping };
struct AreaLease { RegionId area; uint64_t generation{}; Residency residency; };
struct PrepareArea { RegionId destination; uint64_t request{}; GameSettings settings; std::shared_ptr<const pd2::MapInstance> pd2Map{}; uint64_t terrainGeneration{}; };
struct PreparedArea { uint64_t request{}; AreaDefinition definition; };
struct EnvironmentNotice {EnvironmentFact value;uint64_t revision{};};
struct QuestPosition {RegionId area;Vec position;std::optional<RoomView> room{};};
struct State { std::map<RegionId, AreaLease> residency; std::vector<PrepareArea> preparation; };
struct Ports {
    AreaStore &areas; const PlayerStore &players; const GameSettings &settings; EntityIds &ids;
    const travel::System &travel; monsters::System &monsters; const missiles::System &missiles;
    const skills::System &skills; objects::System &objects; const loot::System &loot;
    const spatial::System &spatial; const population::System &population;
    maps::System &maps; items::System &items;
};
class System {
    friend class travel::System;
    friend class quests::System;
    State state_;
    const Ports ports_;
    uint64_t nextRequest_ = 1;
    std::map<RegionId, PrepareArea> failed_;
    struct RoomRuntime;
    std::unique_ptr<RoomRuntime> rooms_;
    std::set<RegionId> openedTombs_;
    std::optional<uint64_t> environmentFrame_;
    size_t environmentAct_{};
    std::optional<EnvironmentNotice> environmentNotice_;
    bool sunDark_{}; // Requested quest mode for an act that may not be loaded yet.
    std::shared_ptr<const pd2::MapInstance> pd2ProgramMap_;
    std::vector<StatValue> pd2RuntimeGlobal_;
    void prepareActTime(int);
    void updateRooms(TickContext);
    void invalidateRooms();
    void refreshRoomTerrain();
    bool roomInputsReady(RegionId) const;
    bool restoreRoomInputs(const AreaDefinition &);
    DomainResult<uint64_t> request(RegionId,std::shared_ptr<const pd2::MapInstance> = {});
    bool cancelPd2Preparation(const std::shared_ptr<const pd2::MapInstance> &);
    void commitPd2Preparation(const std::shared_ptr<const pd2::MapInstance> &) noexcept;
    bool discardPreparedRooms(std::span<const RegionId>);
    void link();
    void assign(AreaDefinition &);
    void registerAreaUnits(RegionId);
  public:
    explicit System(Ports ports);
    ~System();
    const State &read() const { return state_; }
    bool retiringRoom() const;
    bool awaitingRoomInputs() const;
    DomainResult<uint64_t> requestWaypoint(const ActorContext &,RegionId);
    DomainResult<uint64_t> requestTown(const ActorContext &);
    DomainResult<uint64_t> requestArea(const ActorContext &, RegionId);
    DomainResult<uint64_t> requestMapArea(RegionId,const std::shared_ptr<const pd2::MapInstance> &);
    DomainResult<> appendPd2MapGlobal(const std::shared_ptr<const pd2::MapInstance> &,StatValue);
    DomainResult<> install(PreparedArea);
    void initialize();
    void activateAct(RegionId);
    void sunEclipse(bool);
    void setActEclipse(int act,bool dark);
    StepStatus advanceEnvironment(TickContext);
    const std::optional<EnvironmentNotice> &environmentNotice() const {return environmentNotice_;}
    void acknowledgeEnvironment(int act,uint64_t revision);
    void objectCollision(RegionId, std::vector<Grid::Obstacle>);
    void openTombWall(RegionId);
    DomainResult<EntityId> admitQuestNpc(RegionId,std::string_view,Vec,int searchRadius=15);
    std::optional<QuestPosition> questFreePosition(RegionId,Vec roomPosition,Vec requested,int size,int radius,uint16_t mask=0x400) const;
    std::optional<QuestPosition> nativeFreeSpace(RegionId,Vec roomPosition,Vec requested,int size) const;
    std::optional<QuestPosition> nativeLinkPortalPosition(RegionId,bool durielOpening=false);
    std::optional<QuestPosition> nativeLinkPortalPositionFrom(QuestPosition,bool durielOpening=false);
    std::optional<Vec> nativeActSpawnLocation(RegionId,int tile);
    bool nativeProtectedSpawnPosition(RegionId,Vec);
    std::optional<QuestPosition> nativeActSpawnLocationEx(RegionId,int tile,int size,const AreaStore::CollisionSample &sample={});
    std::optional<QuestPosition> nativeActSpawnLocationExAt(RegionId,Vec,int size,const AreaStore::CollisionSample &sample={});
    std::optional<QuestPosition> nativePlayerPosition(RegionId,Vec,EntityId self={}) const;
    std::optional<QuestPosition> nativeWaypointPosition(RegionId,bool changeAct);
    std::optional<QuestPosition> nativeDurielPortalPosition(RegionId);
    void retainControllerRoom(RegionId,Vec,bool retain);
    void retainControllerUnitRoom(EntityId,RegionId,Vec,bool retain,uint32_t reason=0);
    void rebindObject(RegionId,EntityId previous,EntityId current);
    DomainResult<> moveQuestNpc(RegionId,EntityId,Vec,Vec,bool escaped);
    void fail(uint64_t request);
    std::vector<RegionId> visible(PlayerId) const;
    std::vector<RoomView> visibleRooms(PlayerId) const;
    bool clientRoomsReady(PlayerId) const;
    bool visible(PlayerId, RegionId, Vec) const;
    void activate(RegionId, Vec); // Acquires resident terrain; queues released inputs. Caller checks loaded().
    void changeClientRoom(PlayerId); // Current entered player, including initial admission.
    StepStatus step(TickContext, FrameFacts &);
};
}
