#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/effects/definition.hpp"
#include "target_query.hpp"
#include "server/systems/actions/requests.hpp"
#include <deque>
#include <functional>

#include <map>
#include <array>
#include <set>
#include <string>
#include <vector>
namespace d2x::server::monsters {struct Actor;}

namespace d2x::server::ai {
// Decision state only; actions go through skill/movement requests, not direct damage.
struct Controller {
    struct Command {uint8_t code{};UnitTarget target;uint64_t until{};};
    std::deque<Command> commands;
    EntityId actor;
  uint64_t admissionRevision{};
    uint64_t observedAiFunctionRevision{};
    uint64_t observedAiThinkRevision{};
    CurseAi function{CurseAi::None};
    bool alternateAi{};
    std::optional<EntityId> target,carrier;
    uint64_t nextDecision{}, random{}, nextPath{}, observedHit{}, observedDeath{};
    int phase{}, loop{};
    int32_t thirdParameter{};
    bool pursuing{}, charged{}, commanded{}, alerted{};
    uint64_t lastSpawn{};
    bool acquiredTarget{};
    int summonChance{};
    uint64_t firewallCycle{};
    bool introduced{};
    std::array<int32_t,3> curseParameters{};
    uint64_t skillDeadline{},novaAt{},firewallAt{};
    int upgradeCooldown{};
    int regenerationBonus{};
    int act2Attempts{};
    std::optional<Vec> flightGoal;
    std::optional<Vec> homeCommand; // Native command10's global coordinates, captured on first boss think.
    uint64_t randomExpected{};
    bool randomPublished{};
    std::deque<std::function<DomainResult<>()>> pending;
};
struct State { std::map<EntityId, Controller> controllers; };
struct Ports { monsters::System &monsters; const PlayerStore &players; const AreaStore &areas; skills::System &skills; objects::System &objects; const social::System &social; effects::System &effects; const spatial::System &spatial; quests::System &quests; const travel::System &travel; };
class System {
    State state_;
    const Ports ports_;
    DomainResult<> perform(Controller &,std::function<DomainResult<>()>);
    DomainResult<> requestAction(Controller &,const actions::MonsterCastRequest &);
    StepStatus familyAction(EntityId, UnitTarget, Vec, int, TickContext, Controller &,int continuation=0);
    StepStatus fetishShamanAction(EntityId,std::optional<UnitTarget>,Vec,TickContext,Controller &);
    StepStatus act2Action(EntityId, UnitTarget, Vec, int, TickContext, Controller &,int continuation=0);
    StepStatus nestFamily(EntityId,UnitTarget,int,TickContext,Controller &);
    StepStatus vultureFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus frogFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus willowispFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus zakarumZealotFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus zakarumPriestFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus highPriestFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus hydraFamily(EntityId,std::optional<UnitTarget>,int,TickContext,Controller &);
    StepStatus vileFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus fingerMageFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus regurgitatorFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus abyssKnightFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus oblivionKnightFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    bool chargeRayAvailable(const monsters::Actor &,Vec) const;
    StepStatus siegeBeastFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus impFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus genericSpawnerFamily(EntityId,std::optional<UnitTarget>,int,TickContext,Controller &);
    StepStatus minionFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus suicideMinionFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus overseerFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus minionSpawnerFamily(EntityId,std::optional<UnitTarget>,int,TickContext,Controller &);
    StepStatus succubusFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus izualFamily(EntityId,UnitTarget,Vec,int,int,TickContext,Controller &);
    StepStatus mephistoFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus talicFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus madawcFamily(EntityId,TickContext,Controller &);
    StepStatus korlicFamily(EntityId,TickContext,Controller &);
    StepStatus evilHoleFamily(EntityId,int distance,TickContext,Controller &);
    StepStatus tentacleFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus baalTentacleFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus diabloFamily(EntityId,TickContext,Controller &);
    StepStatus baalFamily(EntityId,TickContext,Controller &);
    StepStatus baalToStairsFamily(EntityId,TickContext,Controller &);
    StepStatus baalThroneFamily(EntityId,TickContext,Controller &);
    StepStatus baalTauntFamily(EntityId,TickContext,Controller &);
    StepStatus hellMeteorFamily(EntityId,TickContext,Controller &);
    StepStatus trappedSoulFamily(EntityId,TickContext,Controller &);
    StepStatus catapultFamily(EntityId,TickContext,Controller &);
    StepStatus npcBarbFamily(EntityId,TickContext,Controller &);
    StepStatus catapultSpotterFamily(EntityId,TickContext,Controller &);
    StepStatus nihlathakFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus putridDefilerFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus baalMinionFamily(EntityId,std::optional<UnitTarget>,Vec,int,TickContext,Controller &);
    StepStatus frozenHorrorFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus reanimatedHordeFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    StepStatus megademonFamily(EntityId,std::optional<UnitTarget>,Vec,int,int,TickContext,Controller &);
    std::optional<StepStatus> uniqueAction(EntityId,UnitTarget,Vec,TickContext,Controller &);
    void commandParty(const monsters::Actor &);
    using ShootableTarget=NativeAiTarget;
    std::optional<ShootableTarget> shootableTarget(const monsters::Actor &, TickContext);
    std::optional<ShootableTarget> acquireTarget(const monsters::Actor &,TickContext,bool missileRay,bool requireSight);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    StepStatus step(TickContext, FrameFacts &);
    StepStatus think(EntityId,TickContext);
    void commandMinions(EntityId,UnitTarget,uint64_t until);
};
}
