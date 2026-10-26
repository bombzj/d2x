#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/object_rules.hpp"
#include "server/systems/items/system.hpp"
#include "server/area_store.hpp"

#include <map>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace d2x::server {struct RoomView;struct RoomRandomLoan;}
namespace d2x::server::monsters {struct NativeBirthBatch;}
namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
struct Request { UnitTarget target; };
struct Admission { int definition{}; RegionId area; Vec position; std::optional<int> shrine,trap; int mode{}; uint64_t tick{};std::optional<RoomView> room{}; int pd2Binding{-1};bool collisionReleased{};EntityId nativePortalOwner{};std::optional<pd2::MapPortalProgram> pd2Portal{}; };
struct Object { EntityId id; int definition{}; RegionId area; Vec position; uint64_t revision{}; int mode{}; ObjectRule rule; uint64_t until{}, reset{}; bool pending{}; int uses{}; uint64_t random{},hazardAt{},trapAt{}; int phase{},trap{}; PlayerId operatorId{}; bool specialPending{}; int spawnCount{},spawned{}; bool trapWarned{};uint64_t lootAt{};bool urnPending{},resurrected{};uint8_t sparkChest{},portalDestination{};uint64_t portalAt{}; int pd2Binding{-1};bool collisionReleased{};EntityId nativePortalOwner{};std::optional<pd2::MapPortalProgram> pd2Portal{};EntityId operatorActor{}; };
struct State { std::map<EntityId, Object> objects; std::optional<uint64_t> controlRandom; };
struct ObjectRegionPopulation {
  uint64_t generation{};
  std::set<size_t> rooms;
  int processed{},healingShrines{};
  std::vector<Vec> shrines,wells;
  std::set<int> deferred;
  std::set<int> deferredThemes;
};
struct GroundGenerationRequest {
  uint64_t request{};std::string code;int level{},difficulty{};ItemSeeds seeds;
  std::set<size_t> uniques;
};
struct Ports { const PlayerStore &players; AreaStore &areas; quests::System &quests; loot::System &loot; effects::System &effects; transactions::System &transactions; EntityIds &ids; world::System &world; monsters::System &monsters; const GameSettings &settings; inventory::System &inventory; travel::System &travel; skills::System &skills; EventOutbox &events; combat::System &combat; missiles::System &missiles; MovementSystem &movement; uint64_t &random; const spatial::System &spatial; items::System &items; FrameEventQueue &frameEvents; population::System &population; maps::System &maps; };
class System {
    State state_;
    const Ports ports_;
    struct Approach { ActorContext actor; Request request; uint64_t locomotion; };
    std::map<PlayerId,Approach> approaches_;
    std::vector<EntityId> towerChests_,towerBatch_;
    EntityId towerController_;
    std::optional<Vec> towerDeathPosition_;
    size_t towerProbe_{};
    bool towerFallback_{};
    uint64_t towerRetryAt_{};
    bool towerStarted_{},towerFinished_{};
    std::set<EntityId> towerOpening_;
    std::set<EntityId> towerFailed_;
    std::map<RegionId,ObjectRegionPopulation> population_;
    std::set<EntityId> admittedPresetNpcs_;
    std::set<EntityId> admittedPresets_;
    std::map<EntityId,bool> sandsPresetDecisions_;
    uint64_t sandsPresetCount_{};
    std::map<EntityId,EntityId> presetObjects_;
    std::vector<std::pair<RegionId,Vec>> waypointArrivals_;
    struct CainAttempt {EntityId marker;bool requested{},attempted{};std::set<EntityId> townPlayers;};
    CainAttempt cainAttempt_;
    struct PrisonerGroup {Vec position;unsigned attempts{},spawned{};};
    std::vector<PrisonerGroup> prisonerGroups_;
    bool waypointArrival(const Object &) const;
    void publishInitialization(const Object &);
    DomainResult<bool> spawnTownCain(const AreaNpc &,RegionId,Vec,uint64_t tick);
    StepStatus spawnCagedPrisoners(const AreaObject &,RegionId,uint64_t tick);
    struct InactiveObject {
      EntityId previous,preset;
      RegionId area;Vec position;
      int definition{},mode{},uses{},trap{},shrine{};
      uint64_t frame{},reset{};
      bool locked{};
      uint8_t sparkChest{},portalDestination{};
      int pd2Binding{-1};bool collisionReleased{};EntityId nativePortalOwner{};std::optional<pd2::MapPortalProgram> pd2Portal{};
    };
    std::map<std::pair<RegionId,size_t>,std::vector<InactiveObject>> inactive_;
    struct GroundGeneration {
      GroundGenerationRequest request;
      std::optional<items::PreparedBatch> prepared;
      std::string deferred;
    };
    std::vector<GroundGeneration> groundGeneration_;
    std::optional<std::tuple<RegionId,uint64_t,size_t,uint64_t>> groundGenerationOwner_;
    uint64_t nextGroundRequest_{1};
    struct DamageExecution {std::vector<EntityId> targets;size_t next{};uint64_t occurrence{};bool queued{};};
    std::map<EntityId,DamageExecution> damageExecutions_;
    uint64_t nextDamageOccurrence_{1};
    StepStatus populateRandomRoom(RegionId,size_t,TickContext);
    void collision(RegionId);
    void schedule(Object &);
    void restoreNativePortalReferences(EntityId previous,EntityId current);
    DomainResult<> shrine(const ActorContext &, Object &);
    DomainResult<> specialShrine(const ActorContext &, Object &);
    DomainResult<> shrineMissiles(const ActorContext &,Object &);
    DomainResult<> damage(Object &,DamageType,bool playersOnly,int radius,TickContext,std::optional<EntityId> target={});
    DomainResult<> explode(Object &,const ActorContext &);
    void initialize(Object &,uint64_t tick,uint64_t &random,uint64_t &controlRandom);
    void finishInitialization(Object &,uint64_t &controlRandom);
    DomainResult<> prepareGoldPlaceholder(Object &,uint64_t &random,uint64_t &controlRandom,EntityIds &,
      std::vector<std::pair<items::PreparedBatch,GroundLocation>> &,std::vector<EntityId> &,
      const std::function<uint16_t(RoomView,int32_t,int32_t)> &);
    DomainResult<> prepareSoulPlaceholder(Object &,uint64_t tick,uint64_t &random,uint64_t &controlRandom,EntityIds &,
      monsters::NativeBirthBatch &,std::optional<RoomRandomLoan> &,const std::function<uint16_t(RoomView,int32_t,int32_t)> &);
    DomainResult<EntityId> preparePairedPreset(const Object &,int definition,uint64_t tick,uint64_t &random,EntityIds &,
      monsters::NativeBirthBatch &,std::optional<RoomRandomLoan> &);
    StepStatus hazardStep(TickContext,EntityId);
    struct TrapClock {uint64_t next{},release{},interruption{};int shots{};uint8_t mode{4};Vec aim;EntityId target;bool waiting{};int direction{};uint64_t delayUntil{},admissionRevision{};};
    std::map<EntityId,TrapClock> traps_;
    std::map<RegionId,int> arrowVariants_;
    DomainResult<> spawnTrap(Object &,TickContext);
    DomainResult<> scheduleTrap(Object &,uint64_t tick);
    StepStatus objectTrap(TickContext,EntityId);
    StepStatus monsterTrap(TickContext,EntityId);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    void initializeControl();
    StepStatus populatePresets(RegionId,size_t,TickContext,std::optional<EntityId> only={});
    StepStatus populateRoom(RegionId region,size_t room,TickContext tick) {return populateRandomRoom(region,room,tick);}
    const State &read() const { return state_; }
    const std::map<RegionId,ObjectRegionPopulation> &population() const {return population_;}
    std::optional<GroundGenerationRequest> pendingGroundGeneration() const;
    DomainResult<> prepareGroundGeneration(uint64_t,items::PreparedBatch,std::string);
    void commitLootRandom(EntityId id,uint64_t expected,uint64_t next) {
      const auto found=state_.objects.find(id);
      if(found!=state_.objects.end() && found->second.random==expected) found->second.random=next;
    }
    DomainResult<EntityId> admit(const Admission &);
    void compressRoom(RegionId,size_t,uint64_t tick);
    StepStatus restoreRoom(RegionId,size_t,uint64_t tick);
    DomainResult<> execute(const ActorContext &, const Request &, std::optional<int> remoteRange = {});
    DomainResult<> openMonsterDoor(EntityId,Vec destination,uint64_t tick);
    DomainResult<> openOrbMist(EntityId,uint64_t tick);
    DomainResult<> openNativeBlocker(EntityId,uint64_t tick);
    DomainResult<> linkNativePortals(EntityId source,EntityId destination,bool mutual,uint64_t tick);
    DomainResult<> setNativeMode(EntityId,int mode,uint64_t tick,bool animate=false);
    void activateAncientStatues(const std::array<EntityId,3> &,uint64_t tick);
    DomainResult<> openTowerChest(EntityId,uint64_t tick);
    DomainResult<> towerGold(GroundLocation,uint64_t tick);
    void countessKilled(Vec,uint64_t tick);
    StepStatus towerStep(TickContext);
    void onWaypointArrival(RegionId,Vec);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> frameEvent(const FrameEvent &,uint64_t tick);
    DomainResult<> trapEvent(EntityId,uint64_t tick);
};
}
