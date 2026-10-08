#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "server/systems/transactions/system.hpp"
#include "world/collision.hpp"
#include <map>
#include <memory>
#include <deque>
namespace d2x::server::missiles {
// This request is produced by the skills release frame, never a client missile packet.
struct Spawn { ActorContext actor; SkillCastSpec skill; MissileCollisionRule collision; Vec target;
    bool free{}; EntityId emitter{}; uint8_t emitterType{}; std::optional<Vec> origin{};
    std::optional<WeaponSkillDamage> weapon{};
    std::optional<transactions::Plan> cost{};
    EntityId guidedTarget{};
};
enum class Program { Projectile, Ring, Charged, Orb, OrbBolt, OrbNova, Blizzard, Shard, Arc, FirewallMaker, Fire, Meteor, PoisonCloud, AreaImpact, FuryBolt };
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
    SkillCastSpec skill;
    Program program{Program::Projectile};
    uint64_t random{};
    int ageFrames{}, lifetimeFrames{}, directionIndex{}, remainingHits{};
    EntityId lastHit, emitter; uint8_t emitterType{};
    Vec turnTarget;
    std::deque<Vec> path;
    std::optional<WeaponSkillDamage> weapon{};
    EntityId guidance{};
    int pierces{};
    std::set<EntityId> weaponContacts{};
    bool guidanceSearched{};

};
struct State { std::map<EntityId, Missile> missiles; };
struct Ports {
    const AreaStore &areas; const PlayerStore &players; const monsters::System &monsters;
    combat::System &combat; transactions::System &transactions; EntityIds &ids; uint64_t &random; EventOutbox &events; const effects::System &effects;
};
class System {
    State state_;
    const Ports ports_;
    struct Advance { Missile next; std::vector<Missile> children; std::vector<combat::SpellImpact> impacts; bool finished{}; };
    std::map<EntityId, Advance> pending_;
    Missile make(const Spawn &, Vec origin, Vec direction, int definition, int frames, float speed, Program, uint64_t &) const;
    std::vector<Missile> launch(const Spawn &, uint64_t &) const;
    Advance advance(const Missile &) const;
    Advance advanceWeapon(const Missile &) const;
    void weaponImpact(Advance &, Vec) const;
    combat::SpellImpact impact(Missile &, std::vector<EntityId>, std::optional<int64_t> damage = {}) const;
    std::vector<DomainFact> visuals(const std::vector<Missile> &) const;

  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> spawn(const Spawn &);
    DomainResult<> direct(const Spawn &, std::vector<EntityId> targets);
    StepStatus step(TickContext, FrameFacts &);
};
}
