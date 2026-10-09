#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/units/restoration.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/combat/poison.hpp"
#include <list>
#include <initializer_list>
#include <algorithm>
namespace d2x::server::effects {
struct Cycle { uint64_t next{}; EntityId last; };
struct Recovery { std::map<int,TimedRestoration> healing, mana; CombatEffectSet states; std::map<EffectHandle,Cycle> cycles; std::optional<Vec> previous; std::optional<PoisonStatus> poison; uint64_t nextMissileHit{}; };
struct Reaction { ActorContext actor; EntityId attacker; TriggeredCombatEffect effect; };
struct ItemTrigger {
    ActorContext actor;
    ItemSkillEvent event;
    SkillCastSpec skill;
    MissileCollisionRule collision;
    EntityId target;
    Vec position;
    int chance{},itemEffect{},itemTarget{};
    bool itemTargetDo{},itemCheckStart{},alternativeCompatible{},supported{},resolved{},executed{};
    uint8_t targetType{1};
    std::optional<bool> rolled;
};
struct ItemEventPlan {std::list<ItemTrigger> jobs;};
struct State { std::map<EntityId, Recovery> players; std::string itemDeferred; };
struct UnitEffect { RegionId area; CombatEffectSet states; std::map<int,std::vector<std::pair<int,int64_t>>> nativeStats; };
// Prepared before inventory consumption, installed without allocation only after
// the character transaction succeeds. No callback can observe half a drink.
struct PotionPlan { State next; CharacterRecord character; TransientAttributes transient; std::optional<uint64_t> random{}; };
struct Ports {
    const PlayerStore &players; const AreaStore &areas; skills::System &skills;
    transactions::System &transactions; missiles::System &missiles; combat::System &combat; monsters::System &monsters; EventOutbox &events; uint64_t &random;
};
class System {
    State state_;
    const Ports ports_;
    std::deque<Reaction> reactions_;
    std::list<ItemTrigger> itemTriggers_;
    StepStatus advanceItemTriggers(uint64_t);
    std::map<EntityId,UnitEffect> units_;
    struct WebTrail {Vec previous,origin,target;RegionId area;uint64_t until{},random{};bool pending{};};
    std::map<EntityId,WebTrail> webTrails_;
    struct UniqueCycle {uint64_t damage{},lightning{},nextLightning{},death{},deathOccurrence{},nextAura{},auraOccurrence{},random{};int deathPhase{};bool cursePending{},auraPending{};size_t auraTarget{},curseTarget{};std::vector<EntityId> targets,curseTargets;};
    std::map<EntityId,UniqueCycle> uniqueCycles_;
    StepStatus advanceMonsterEnchantments(uint64_t);
    DomainResult<> monsterAura(EntityId,const AuraDefinition &,EntityId,uint64_t,uint64_t duration,bool owner=false);

    StepStatus advanceSkills(const ActorContext &, Recovery &);
    StepStatus advanceReactions(uint64_t);
    StepStatus advanceUnits(uint64_t);
    StepStatus advanceMonsterSkills(uint64_t);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    size_t pendingItemTriggers() const {return itemTriggers_.size();}
    DomainResult<ItemEventPlan> prepareItemEvents(const ActorContext &,std::initializer_list<ItemSkillEvent>,EntityId target,Vec position) const;
    void commitItemEvents(ItemEventPlan plan,bool terminal=true) noexcept {
        if(!terminal) std::erase_if(plan.jobs,[](const auto &job){return job.event==ItemSkillEvent::Death || job.event==ItemSkillEvent::Kill;});
        itemTriggers_.splice(itemTriggers_.end(),plan.jobs);
    }
    DomainResult<PotionPlan> potion(const ActorContext &, const PotionDefinition &) const;
    DomainResult<PotionPlan> heal(const ActorContext &) const;
    void commit(PotionPlan &&plan) noexcept { state_.players.swap(plan.next.players); if (plan.random) ports_.random = *plan.random; }
    DomainResult<> apply(const ActorContext &, CombatEffectSpec, bool restoreStamina = false);
    DomainResult<> skill(const ActorContext &, const SkillCastSpec &, std::optional<PlayerId> recipient = {});
    DomainResult<> skillUnit(const ActorContext &, const SkillCastSpec &, EntityId);
    DomainResult<> amazonMagic(const ActorContext &, const SkillCastSpec &);
    CharacterModifiers unitModifiers(EntityId, uint64_t tick) const;
    int unitDefense(EntityId,uint64_t tick) const;
    int unitResistance(EntityId,DamageType,uint64_t tick) const;
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    std::set<int> unitStates(EntityId, uint64_t tick) const;
    std::map<int,std::vector<std::pair<int,int64_t>>> unitStateStats(EntityId, uint64_t tick) const;
    DomainResult<float> receive(const ActorContext &, int64_t rawDamage, DamageType);
    DomainResult<float> receiveMonster(const ActorContext &, EntityId source, const MonsterHit &, const MonsterHitStates &,std::optional<ItemHandle> wear = {});
    bool missileHitAllowed(EntityId,uint64_t tick) const;
    void missileHitDelay(EntityId,uint64_t until);
    void triggerMonsterCurse(EntityId,uint64_t tick);
    bool reactionCapacity() const { return reactions_.size() <= 4096-128; }
    void react(const ActorContext &, EntityId attacker, CombatEffectEvent, bool returnFire = true);
    StepStatus step(TickContext, FrameFacts &);
};
}
