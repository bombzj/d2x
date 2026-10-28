#pragma once
#include "requests.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "participants.hpp"
#include "server/runtime/combat_rules.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::actions {struct State;}
namespace d2x::server::combat {
struct NativeLifeNotification {uint64_t tick{};uint8_t sentLife{};bool dirty{};};
struct State { std::list<SpellImpact> spells; size_t spellTargets{}; uint32_t hitClassCursor{}; uint64_t targetAttempts{}, reflectionOccurrence{},progressiveOccurrence{}; std::map<EntityId,NativeLifeNotification> lifeNotifications; std::map<EntityId,uint64_t> pd2AvoidUntil; };
struct Ports { const PlayerStore &players; monsters::System &monsters; const AreaStore &areas; transactions::System &transactions; EventOutbox &events; effects::System &effects; inventory::System &inventory; const social::System &social; reactions::ItemTriggers &itemTriggers; missiles::System &missiles; actions::State &actions; uint32_t &pd2Random; };
class System {
    State state_;
    const Ports ports_;
    void storeLastAttacker(SpellImpact &,ParticipantView,EntityId source);
    StepStatus resolveSpells(TickContext,std::optional<std::pair<EntityId,uint64_t>> selected={},bool allocateOnly=false);
    DomainStatus resolveEnvironment(SpellImpact &,TickContext);
    DomainStatus prepareMissileContact(SpellImpact &,ParticipantView,ParticipantView,uint64_t generation,TickContext,bool preludeOnly=false);
    DomainStatus prepareNativeSkillContact(SpellImpact &,ParticipantView,ParticipantView,TickContext);
    DomainStatus postNativeMeleeAllocation(SpellImpact &,TickContext);
    DomainStatus prepareNativeSmite(SpellImpact &,const PlayerState &,TickContext);
    DomainStatus preparePd2SplashDamage(SpellImpact &,ParticipantView,TickContext);
    DomainStatus preparePd2SplashDefense(SpellImpact &,ParticipantView,ParticipantView,TickContext);
    DomainStatus preparePd2MissileCritical(SpellImpact &,ParticipantView,TickContext);
    void preparePd2LifeNotification(ParticipantView,TickContext);
    void finishPd2LifeNotification(SpellImpact &,ParticipantView,bool getHit,bool softHit,int64_t damage,bool recovery,TickContext);
    DomainStatus prepareContactProcedures(SpellImpact &,ParticipantView,ParticipantView,TickContext);
    DomainStatus prepareMeleeDrainProcedures(SpellImpact &,ParticipantView,TickContext);
    bool nativeMeleeReach(const SpellImpact &,int range=-1) const;
    DomainStatus finishNativeMeleeMiss(SpellImpact &,ParticipantView,TickContext);
    DomainResult<bool> prepareWeaponBlock(ParticipantView,TickContext,WeaponBlockCursor &);
    DomainStatus finishNativeSkillFlags(SpellImpact &,TickContext);
    DomainResult<bool> advanceProgressive(SpellImpact &,TickContext,bool mainDrained=false);
    DomainStatus finishMissileArmor(SpellImpact &,ParticipantView,TickContext);
    DomainStatus advanceMissileDrain(SpellImpact &,ParticipantView,int64_t physical,int64_t life,int64_t mana,int64_t stamina,TickContext);
    DomainStatus resolvePlayerImpact(SpellImpact &,ParticipantView,const PlayerState &,const PlayerState &,uint64_t generation,TickContext);
    DomainStatus finishPlayerContact(SpellImpact &,const PlayerState &,TickContext);
    DomainStatus finishMonsterContact(SpellImpact &,const monsters::Actor &,TickContext);
    DomainStatus finishImpact(SpellImpact &, const PlayerState *, uint64_t generation, uint64_t tick);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> monsterMelee(EntityId source,EntityId target,const MonsterAttackRule &,uint64_t occurrence,TickContext);
    DomainResult<MissilePreludeResult> missilePrelude(SpellImpact &,TickContext);
    bool weaponContact(const WeaponSkillDamage &,EntityId,uint64_t,uint64_t &) const;
    DomainResult<bool> directDefense(SpellImpact &,ParticipantView,ParticipantView,TickContext);
    DomainResult<bool> sampleNativeSkillAccuracy(const ActorContext &,const SkillCastSpec &,const WeaponDamage &,EntityId,std::shared_ptr<NativeSkillContact> &);
    DomainResult<> enqueue(SpellImpact);
    DomainResult<SpellPlan> prepareSpells(std::vector<SpellImpact>) const;
    void commitSpells(SpellPlan &&) noexcept;
    StepStatus advanceAuraPulse(EntityId source,uint64_t occurrence,TickContext tick) {return resolveSpells(tick,std::pair{source,occurrence});}
    StepStatus advanceEnvironmentImpact(EntityId source,uint64_t occurrence,TickContext tick) {return resolveSpells(tick,std::pair{source,occurrence});}
    StepStatus allocateNativeMelee(EntityId source,uint64_t occurrence,TickContext tick) {return resolveSpells(tick,std::pair{source,occurrence},true);}
    StepStatus drainNativeMelee(EntityId source,uint64_t occurrence,TickContext tick);
    bool nativeMeleeDrainReach(SpellImpact &);
    DomainStatus postNativeSkillCharge(SpellImpact &,TickContext);
    DomainStatus finishNativeMeleeDrain(SpellImpact &,int64_t physical,TickContext);
    void discardNativeMelee(EntityId source,uint64_t occurrence);
    DomainResult<> itemMeleeDrain(EntityId source,EntityId target,TickContext,std::shared_ptr<SpellImpact> &);
    bool addAllocatedPhysical(EntityId source,uint64_t occurrence,int32_t amount);
    void cancel(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
