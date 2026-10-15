#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/object_rules.hpp"
#include "server/systems/items/system.hpp"

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
struct Request { UnitTarget target; };
struct Admission { int definition{}; RegionId area; Vec position; std::optional<int> shrine,trap; int mode{}; uint64_t tick{}; };
struct Object { EntityId id; int definition{}; RegionId area; Vec position; uint64_t revision{}; int mode{}; ObjectRule rule; uint64_t until{}, reset{}; bool pending{}; int uses{}; uint64_t random{},hazardAt{},trapAt{}; int phase{},trap{}; PlayerId operatorId; bool specialPending{}; int spawnCount{},spawned{},secondaryTrap{}; bool trapWarned{};uint64_t lootAt{}; };
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
struct Ports { const PlayerStore &players; AreaStore &areas; quests::System &quests; loot::System &loot; effects::System &effects; transactions::System &transactions; EntityIds &ids; world::System &world; monsters::System &monsters; const GameSettings &settings; inventory::System &inventory; travel::System &travel; skills::System &skills; EventOutbox &events; combat::System &combat; missiles::System &missiles; MovementSystem &movement; uint64_t &random; const spatial::System &spatial; items::System &items; };
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
    struct GroundGeneration {
      GroundGenerationRequest request;
      std::optional<items::PreparedBatch> prepared;
      std::string deferred;
    };
    std::vector<GroundGeneration> groundGeneration_;
    std::optional<std::tuple<RegionId,uint64_t,size_t,uint64_t>> groundGenerationOwner_;
    uint64_t nextGroundRequest_{1};
    StepStatus populateRandomRoom(RegionId,size_t,TickContext);
    void collision(RegionId);
    DomainResult<> shrine(const ActorContext &, Object &);
    DomainResult<> specialShrine(const ActorContext &, Object &);
    DomainResult<> shrineMissiles(const ActorContext &,Object &);
    DomainResult<> damage(Object &,DamageType,bool playersOnly,int radius,TickContext,std::optional<EntityId> target={});
    DomainResult<> explode(Object &,const ActorContext &);
    void initialize(Object &,uint64_t tick,uint64_t &random,uint64_t &controlRandom);
    StepStatus hazardStep(TickContext);
    struct TrapClock {uint64_t next{},release{},interruption{};int shots{};uint8_t mode{4};Vec aim;EntityId target;bool waiting{};int direction{};uint64_t delayUntil{};};
    std::map<EntityId,TrapClock> traps_;
    std::map<RegionId,int> arrowVariants_;
    DomainResult<> spawnTrap(Object &,TickContext);
    StepStatus trapStep(TickContext);
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
    DomainResult<> execute(const ActorContext &, const Request &, std::optional<int> remoteRange = {});
    DomainResult<> openMonsterDoor(EntityId,Vec destination,uint64_t tick);
    DomainResult<> openOrbMist(EntityId,uint64_t tick);
    DomainResult<> openTowerChest(EntityId,uint64_t tick);
    DomainResult<> towerGold(GroundLocation,uint64_t tick);
    void countessKilled(Vec,uint64_t tick);
    StepStatus towerStep(TickContext);
    void onWaypointArrival(RegionId,TickContext);
    StepStatus step(TickContext, FrameFacts &);
};
}
