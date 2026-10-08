#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "server/runtime/combat_rules.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::monsters {
// Sole owner of monster/NPC/companion actor runtime; content identity is preserved.
struct Admission { MonsterIdentity identity; std::optional<MonsterKind> implementation; RegionId area; Vec position; bool hostile{}; std::optional<MonsterRule> rule; };
struct MoveRequest {
    EntityId actor; PointTarget destination; EntityId target;
    int stopDistance{}, velocityPercent{75}; bool running{};
};
struct Actor {
    EntityId id; MonsterIdentity identity; std::optional<MonsterKind> implementation;
    RegionId area; Vec position; uint64_t revision{}; std::optional<PlayerId> owner;
    MonsterRule rule;
    int64_t life{}, maximumLife{};
    std::deque<Vec> route;
    uint64_t busyUntil{}, deathTick{}, deathOccurrence{};
    EntityId killer, movementTarget;
    int stopDistance{}, velocityPercent{75};
    bool running{};
    bool rewardComplete{}, moving{};
};
struct State { std::map<EntityId, Actor> actors; };
struct Ports { const AreaStore &areas; const PlayerStore &players; EntityIds &ids; uint64_t &random; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    const Actor *find(EntityId id) const { auto it = state_.actors.find(id); return it == state_.actors.end() ? nullptr : &it->second; }
    DomainResult<> beginAttack(EntityId, uint64_t until);
    DomainResult<> damage(EntityId, EntityId source, int64_t amount, uint64_t tick);
    void rewardComplete(EntityId);
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> requestMove(const MoveRequest &);
    void stop(EntityId);
    DomainResult<> remove(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
