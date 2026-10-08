#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "world/collision.hpp"
#include <map>
namespace d2x::server::missiles {
// This request is produced by the skills release frame, never a client missile packet.
struct Spawn { ActorContext actor; SkillCastSpec skill; MissileCollisionRule collision; Vec target; };
struct Missile {
    EntityId id, owner;
    PlayerId player;
    int definition{};
    RegionId area;
    uint64_t generation{}, created{}, expires{}, revision{};
    Vec position, velocity;
    float acceleration{}, maximumVelocity{}, radius{};
    MissileCollisionRule collision;
    int64_t damage{};
    std::optional<combat::SpellImpact> impact;
};
struct State { std::map<EntityId, Missile> missiles; };
struct Ports {
    const AreaStore &areas; const PlayerStore &players; const monsters::System &monsters;
    combat::System &combat; transactions::System &transactions; EntityIds &ids; uint64_t &random;
};
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> spawn(const Spawn &);
    StepStatus step(TickContext, FrameFacts &);
};
}
