#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/avoidance.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::skills {
// Skill selection and cast/channel lifecycle; formulas use prepared pure skill definitions.
enum class Action { Select, Cast, Stop, Bind };
struct Request { Action action; uint16_t skill{}; bool right{}, repeat{}, stationary{}; std::optional<ActionTarget> target; std::optional<unsigned> hotkey; };
// Internal AI/companion action, not a player identity supplied over the wire.
struct CastRequest { EntityId actor; uint16_t skill{}; ActionTarget target; uint64_t tick{}; uint8_t monsterMode{4}; std::optional<Vec> position{}; int teleportHeal{}; };
struct Cast { EntityId actor; uint16_t skill{}; uint64_t started{}, revision{}, until{}; RegionId area; EntityId target; bool interrupted{}; uint64_t cooldownUntil{}; };
struct State { std::map<EntityId, Cast> casts; };
struct Ports { const PlayerStore &players; const AreaStore &areas; monsters::System &monsters; MovementSystem &movement; combat::System &combat; transactions::System &transactions; missiles::System &missiles; travel::System &travel; EventOutbox &events; effects::System &effects; companions::System &companions; objects::System &objects; inventory::System &inventory; };
class System {
    State state_;
    const Ports ports_;
    struct Pending { ActorContext actor; Request request; };
    std::map<PlayerId, Pending> pending_;
    struct Release {
        ActorContext actor;
        SkillCastSpec skill;
        MissileCollisionRule collision;
        PointTarget target;
        EntityId unit;
        uint64_t tick{};
        uint8_t unitType{1}; uint64_t pulses{}; bool manaPaid{};
        std::optional<WeaponDamage> weapon{};
        std::vector<int> weaponHits{};
        size_t nextWeaponHit{};
        int weaponSpeed{}, weaponFrames{}, weaponRollback{};
    };
    std::map<EntityId, Release> releases_;
    struct MonsterRelease {
        CastRequest request;
        MonsterAttackRule attack;
        RegionId area;
        uint64_t generation{}, due{}, random{};
        uint64_t interruption{};
        std::optional<std::pair<Vec,Vec>> launch;
        std::vector<int> releaseFrames{};
        size_t nextRelease{};
        uint64_t started{};
    };
    std::map<EntityId,MonsterRelease> monsterReleases_;
    StepStatus releaseMonsters(TickContext);
    DomainResult<> cast(const ActorContext &, const Request &, int skill);
    DomainResult<> weaponCast(const ActorContext &, const Request &, int);
    DomainStatus weaponRelease(Release &, const ActorContext &, Vec);
    StepStatus release(TickContext);
    DomainStatus activate(Release &, const ActorContext &, Vec);
    std::optional<Vec> unitPosition(const ActorContext &, UnitTarget, SkillBehavior) const;
    DomainResult<> attack(const ActorContext &, const Request &, int selectedOverride = -1);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    void cancel(PlayerId, EntityId);
    size_t pendingReleases() const { return releases_.size()+monsterReleases_.size(); }
    bool busy(EntityId id, uint64_t tick) const { auto it = state_.casts.find(id); return releases_.contains(id) || monsterReleases_.contains(id) || (it != state_.casts.end() && it->second.until > tick); }
    DomainResult<> requestCast(const CastRequest &);
    // Native OperateFn05 invokes the hidden Kick without changing either hand.
    DomainResult<> objectKick(const ActorContext &, UnitTarget);
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
