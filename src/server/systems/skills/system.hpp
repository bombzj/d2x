#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/avoidance.hpp"
#include <memory>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x { struct SkillCastSpec; enum class SkillBehavior; }
namespace d2x::server::skills {
// Skill selection and cast/channel lifecycle; formulas use prepared pure skill definitions.
enum class Action { Select, Cast, Stop, Bind };
struct Request { Action action; uint16_t skill{}; bool right{}, repeat{}, stationary{}; std::optional<ActionTarget> target; std::optional<unsigned> hotkey; uint32_t owner=UINT32_MAX; };
// Internal AI/companion action, not a player identity supplied over the wire.
struct CastRequest { EntityId actor; uint16_t skill{}; ActionTarget target; uint64_t tick{}; uint8_t monsterMode{4}; std::optional<Vec> position{}; int teleportHeal{}; };
struct Cast { EntityId actor; uint16_t skill{}; uint64_t started{}, revision{}, until{}; RegionId area; EntityId target; bool interrupted{}; uint64_t cooldownUntil{}; };
struct State { std::map<EntityId, Cast> casts; };
struct Ports { const PlayerStore &players; const AreaStore &areas; monsters::System &monsters; MovementSystem &movement; combat::System &combat; transactions::System &transactions; missiles::System &missiles; travel::System &travel; EventOutbox &events; effects::System &effects; companions::System &companions; objects::System &objects; inventory::System &inventory; };
class System {
    State state_;
    const Ports ports_;
    struct Release;
    struct MonsterRelease;
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
    StepStatus releaseMonsters(TickContext);
    DomainResult<> cast(const ActorContext &, const Request &, int skill);
    DomainResult<> weaponCast(const ActorContext &, const Request &, int);
    DomainStatus weaponRelease(Release &, const ActorContext &, Vec);
    StepStatus release(TickContext);
    DomainStatus activate(Release &, const ActorContext &, Vec);
    enum class ActivationProgram {
        Weapon, Item, Unsummon, Kick, AmazonSummon, AmazonMagic,
        Teleport, Hydra, Effect, StaticField, Telekinesis, Missile, Unsupported, Count
    };
    static ActivationProgram activationProgram(const SkillCastSpec &);
    DomainStatus activateItem(Release &, const ActorContext &, Vec);
    DomainStatus activateUnsummon(Release &, const ActorContext &, Vec);
    DomainStatus activateKick(Release &, const ActorContext &, Vec);
    DomainStatus activateAmazonSummon(Release &, const ActorContext &, Vec);
    DomainStatus activateAmazonMagic(Release &, const ActorContext &, Vec);
    DomainStatus activateTeleport(Release &, const ActorContext &, Vec);
    DomainStatus activateHydra(Release &, const ActorContext &, Vec);
    DomainStatus activateEffect(Release &, const ActorContext &, Vec);
    DomainStatus activateStaticField(Release &, const ActorContext &, Vec);
    DomainStatus activateTelekinesis(Release &, const ActorContext &, Vec);
    DomainStatus activateMissile(Release &, const ActorContext &, Vec);
    DomainStatus activateUnsupported(Release &, const ActorContext &, Vec);
    std::vector<EntityId> staticFieldTargets(const ActorContext &, const SkillCastSpec &) const;
    std::optional<Vec> unitPosition(const ActorContext &, UnitTarget, SkillBehavior) const;
    DomainResult<> attack(const ActorContext &, const Request &, int selectedOverride = -1);
  public:
    explicit System(Ports ports);
    ~System();
    System(const System &)=delete;
    System &operator=(const System &)=delete;
    const State &read() const { return state_; }
    void cancel(PlayerId, EntityId);
    bool uninterruptible(EntityId actor) const;
    size_t pendingReleases() const;
    bool busy(EntityId id, uint64_t tick) const;
    DomainResult<> requestCast(const CastRequest &);
    DomainResult<> itemTrigger(const ActorContext &,SkillCastSpec,MissileCollisionRule,EntityId,Vec,bool dead,bool itemTargetDo);
    // Native OperateFn05 invokes the hidden Kick without changing either hand.
    DomainResult<> objectKick(const ActorContext &, UnitTarget);
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
