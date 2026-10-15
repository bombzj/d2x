#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/object_rules.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
struct Request { UnitTarget target; };
struct Admission { int definition{}; RegionId area; Vec position; std::optional<int> shrine,trap; int mode{}; uint64_t tick{}; };
struct Object { EntityId id; int definition{}; RegionId area; Vec position; uint64_t revision{}; int mode{}; ObjectRule rule; uint64_t until{}, reset{}; bool pending{}; int uses{}; uint64_t random{},hazardAt{},trapAt{}; int phase{},trap{}; PlayerId operatorId; bool specialPending{}; int spawnCount{},spawned{},secondaryTrap{}; bool trapWarned{};uint64_t lootAt{}; };
struct State { std::map<EntityId, Object> objects; };
struct TowerClock { int remaining{}; bool opened{}; uint64_t random{}; };
struct Ports { const PlayerStore &players; AreaStore &areas; quests::System &quests; loot::System &loot; effects::System &effects; transactions::System &transactions; EntityIds &ids; world::System &world; monsters::System &monsters; const GameSettings &settings; inventory::System &inventory; travel::System &travel; skills::System &skills; EventOutbox &events; combat::System &combat; missiles::System &missiles; MovementSystem &movement; };
class System {
    State state_;
    const Ports ports_;
    struct Approach { ActorContext actor; Request request; uint64_t locomotion; };
    std::map<PlayerId,Approach> approaches_;
    std::map<EntityId,TowerClock> towerClocks_;
    void towerStep(TickContext);
    void collision(RegionId);
    DomainResult<> shrine(const ActorContext &, Object &);
    DomainResult<> specialShrine(const ActorContext &, Object &);
    DomainResult<> shrineMissiles(const ActorContext &,Object &);
    DomainResult<> damage(Object &,DamageType,bool playersOnly,int radius,TickContext,std::optional<EntityId> target={});
    DomainResult<> explode(Object &,const ActorContext &);
    void initialize(Object &,uint64_t tick);
    StepStatus hazardStep(TickContext);
    struct TrapClock {uint64_t next{},release{},interruption{};int shots{};uint8_t mode{4};Vec aim;EntityId target;bool waiting{};int direction{};uint64_t delayUntil{};};
    std::map<EntityId,TrapClock> traps_;
    std::map<RegionId,int> arrowVariants_;
    DomainResult<> spawnTrap(Object &,TickContext);
    StepStatus trapStep(TickContext);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> execute(const ActorContext &, const Request &, std::optional<int> remoteRange = {});
    DomainResult<> openMonsterDoor(EntityId,Vec destination,uint64_t tick);
    DomainResult<> openOrbMist(EntityId,uint64_t tick);
    void onWaypointArrival(RegionId,TickContext);
    StepStatus step(TickContext, FrameFacts &);
};
}
