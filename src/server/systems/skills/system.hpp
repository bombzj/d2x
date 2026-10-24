#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/actions/state.hpp"
#include "server/systems/actions/requests.hpp"
#include "gameplay/actions/weapon_timing.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/skills/activation_program.hpp"
#include <memory>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x { struct SkillCastSpec; enum class SkillBehavior; }
namespace d2x::server::reactions { class ItemTriggers; }
namespace d2x::server::skills {
// Skill selection and cast/channel lifecycle; formulas use prepared pure skill definitions.
enum class Action { Select, Cast, Stop, Bind };
struct Request { Action action; uint16_t skill{}; bool right{}, repeat{}, stationary{}; std::optional<ActionTarget> target; std::optional<unsigned> hotkey; uint32_t owner=UINT32_MAX; };
// Internal AI/companion action, not a player identity supplied over the wire.
using CastRequest = actions::MonsterCastRequest;
using Cast = actions::SkillAction;
using State = actions::State;
struct Ports { const PlayerStore &players; const AreaStore &areas; monsters::System &monsters; MovementSystem &movement; combat::System &combat; transactions::System &transactions; missiles::System &missiles; travel::System &travel; EventOutbox &events; effects::System &effects; companions::System &companions; objects::System &objects; inventory::System &inventory; loot::System &loot; const social::System &social; actions::State &actions; reactions::ItemTriggers &itemTriggers; const spatial::System &spatial; FrameEventQueue &frameEvents; ai::System &ai; uint32_t &pd2Random; };
class System {
  State &state_;
    const Ports ports_;
    using Release = actions::SkillRelease;
    using MonsterRelease = actions::MonsterRelease;
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
    StepStatus releaseMonsters(TickContext,EntityId);
    DomainResult<bool> releaseAssassinPet(MonsterRelease &,TickContext);
    DomainResult<bool> releaseMonsterInferno(MonsterRelease &,TickContext);
    DomainResult<> startMonsterWhirlwind(const CastRequest &,const MonsterAttackRule &);
    DomainResult<> monsterWhirlwindEvent(const FrameEvent &,uint64_t);
    DomainResult<> startMonsterLeapAttack(const CastRequest &,const MonsterAttackRule &);
    DomainResult<> monsterLeapAttackEvent(const FrameEvent &,uint64_t);
    DomainResult<bool> releaseMonsterMosquito(MonsterRelease &,TickContext);
    DomainResult<> releaseOverseer(MonsterRelease &,TickContext);
    DomainResult<> releaseMonsterSpell(MonsterRelease &,TickContext);
    DomainResult<> monsterRunEvent(const FrameEvent &,uint64_t tick);
    DomainResult<Vec> startMonsterLeap(const CastRequest &);
    DomainResult<bool> releaseMonsterLeap(MonsterRelease &,TickContext);
    DomainResult<bool> releaseDruidPet(MonsterRelease &,TickContext);
    DomainResult<> startDruidPet(MonsterRelease &,TickContext);
    DomainResult<bool> releaseShadowSkill(MonsterRelease &,TickContext);
    DomainResult<std::shared_ptr<actions::SkillRelease>> startShadowSkill(const CastRequest &,const MonsterAttackRule &);
    DomainResult<> cast(const ActorContext &, const Request &, int skill);
    DomainResult<> weaponCast(const ActorContext &, const Request &, int);
    DomainStatus weaponRelease(Release &, const ActorContext &, Vec);
    StepStatus release(TickContext,EntityId);
    DomainStatus activate(Release &, const ActorContext &, Vec);
    using ActivationProgram = SkillActivationProgram;
    static ActivationProgram activationProgram(const SkillCastSpec &);
    DomainStatus activateItem(Release &, const ActorContext &, Vec);
    DomainStatus activateUnsummon(Release &, const ActorContext &, Vec);
    DomainStatus activateKick(Release &, const ActorContext &, Vec);
    DomainStatus activateAmazonSummon(Release &, const ActorContext &, Vec);
    DomainStatus activateAmazonMagic(Release &, const ActorContext &, Vec);
    DomainStatus activateTeleport(Release &, const ActorContext &, Vec);
    DomainStatus activatePd2Teleport(Release &, const ActorContext &, Vec);
    DomainStatus activateLeap(Release &,const ActorContext &,Vec);
    DomainStatus activateHydra(Release &, const ActorContext &, Vec);
    DomainStatus activateEffect(Release &, const ActorContext &, Vec);
    DomainStatus activatePd2Enchant(Release &, const ActorContext &);
    DomainStatus activatePd2(Release &,const ActorContext &,Vec);
    DomainStatus activatePd2Splash(Release &,const ActorContext &);
    DomainStatus activatePd2Bash(Release &,const ActorContext &);
    DomainStatus activatePd2Raven(Release &,const ActorContext &,Vec);
    DomainStatus activatePd2HolyLight(Release &,const ActorContext &);
    DomainStatus activatePd2Leap(Release &,const ActorContext &,Vec);
    DomainStatus activatePd2Joust(Release &,const ActorContext &,Vec);
    DomainStatus activateWarcry(Release &,const ActorContext &,Vec);
    DomainStatus activateCurse(Release &, const ActorContext &, Vec);
    DomainStatus activateCorpse(Release &,const ActorContext &,Vec);
    DomainStatus activateCorpseSearch(Release &,const ActorContext &,Vec);
    DomainStatus activateGrimWard(Release &,const ActorContext &,Vec);
    DomainStatus activateWhirlwind(Release &,const ActorContext &,Vec);
    DomainStatus activateStaticField(Release &, const ActorContext &, Vec);
    DomainStatus activateTelekinesis(Release &, const ActorContext &, Vec);
    DomainStatus activateMissile(Release &, const ActorContext &, Vec);
    DomainStatus activateAssassin(Release &, const ActorContext &, Vec);
    DomainStatus activateDruid(Release &, const ActorContext &, Vec);
    DomainStatus startDruidPeriodic(Release &,const ActorContext &);
    DomainResult<bool> startArctic(Release &,const ActorContext &,std::span<const DomainFact> facts={});
    DomainResult<> startDruidMelee(Release &,const ActorContext &);
    DomainResult<> startWeaponMelee(Release &,const ActorContext &,const MonsterAttackRule *program=nullptr);
    DomainStatus drainWeaponMelee(Release &,const ActorContext &);
    DomainStatus activateDruidMelee(Release &,const ActorContext &,Vec);
    DomainStatus allocateDruidMelee(Release &,const ActorContext &,const SkillCastSpec &,bool held);
    DomainResult<bool> startBladeFury(Release &,const ActorContext &,std::span<const DomainFact> facts={});
    DomainResult<> startAssassinKick(Release &,const ActorContext &);
    DomainStatus activateAssassinKick(Release &,const ActorContext &,Vec);
    DomainStatus allocateAssassinKick(Release &,const ActorContext &,bool held,bool knockback);
    DomainStatus activateUnsupported(Release &, const ActorContext &, Vec);
    std::optional<Vec> unitPosition(const ActorContext &, UnitTarget, SkillBehavior) const;
    DomainResult<> attack(const ActorContext &, const Request &, int selectedOverride = -1);
  public:
    explicit System(Ports ports);
    ~System();
    System(const System &)=delete;
    System &operator=(const System &)=delete;
    const State &read() const { return state_; }
    void cancel(PlayerId, EntityId);
    DomainResult<> ejectImp(EntityId carrier,uint64_t tick);
    void interrupt(const ActorContext &);
    bool uninterruptible(EntityId actor) const;
    size_t pendingReleases() const;
    bool busy(EntityId id, uint64_t tick) const;
    DomainResult<> requestCast(const CastRequest &);
    DomainResult<> itemTrigger(const ActorContext &,SkillCastSpec,MissileCollisionRule,EntityId,Vec,bool dead,bool itemTargetDo,std::shared_ptr<actions::SkillRelease> *continuation=nullptr);
    // Native OperateFn05 invokes the hidden Kick without changing either hand.
    DomainResult<> objectKick(const ActorContext &, UnitTarget);
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> activeStateEvent(const FrameEvent &,uint64_t tick);
    DomainResult<> actionEvent(const FrameEvent &,uint64_t tick);
    DomainResult<> monsterActionEvent(const FrameEvent &,uint64_t tick);
};
}
