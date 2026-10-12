#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>
namespace d2x::server::monsters {struct Actor;}

namespace d2x::server::ai {
// Decision state only; actions go through skill/movement requests, not direct damage.
struct Controller {
    EntityId actor;
    std::optional<EntityId> target;
    uint64_t nextDecision{}, random{}, nextPath{}, observedHit{}, observedDeath{};
    int phase{}, loop{};
    bool pursuing{}, charged{}, commanded{}, alerted{};
    uint64_t lastSpawn{};
    bool acquiredTarget{};
    int summonChance{};
    uint64_t firewallCycle{};
    bool introduced{};
    uint64_t skillDeadline{},novaAt{},firewallAt{};
    int upgradeCooldown{};
    int act2Attempts{};
    std::optional<Vec> flightGoal;
};
struct State { std::map<EntityId, Controller> controllers; };
struct Ports { monsters::System &monsters; const PlayerStore &players; const AreaStore &areas; skills::System &skills; objects::System &objects; uint64_t &random; const social::System &social; };
class System {
    State state_;
    const Ports ports_;
    StepStatus familyAction(EntityId, UnitTarget, Vec, int, TickContext, Controller &);
    StepStatus act2Action(EntityId, UnitTarget, Vec, int, TickContext, Controller &);
    StepStatus nestFamily(EntityId,UnitTarget,int,TickContext,Controller &);
    std::optional<StepStatus> uniqueAction(EntityId,UnitTarget,Vec,TickContext,Controller &);
    void commandParty(const monsters::Actor &);
    struct ShootableTarget { UnitTarget unit; Vec position; int size{}, distance{}; };
    std::optional<ShootableTarget> shootableTarget(const monsters::Actor &, TickContext) const;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    StepStatus step(TickContext, FrameFacts &);
};
}
