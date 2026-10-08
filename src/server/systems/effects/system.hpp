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
namespace d2x::server::effects {
struct Cycle { uint64_t next{}; EntityId last; };
struct Recovery { std::deque<ResourceRestoration> healing, mana; CombatEffectSet states; std::map<EffectHandle,Cycle> cycles; std::optional<Vec> previous; };
struct Reaction { ActorContext actor; EntityId attacker; TriggeredCombatEffect effect; };
struct State { std::map<EntityId, Recovery> players; };
struct UnitEffect { RegionId area; CombatEffectSet states; std::map<int,std::vector<std::pair<int,int64_t>>> nativeStats; };
// Prepared before inventory consumption, installed without allocation only after
// the character transaction succeeds. No callback can observe half a drink.
struct PotionPlan { State next; CharacterRecord character; TransientAttributes transient; };
struct Ports {
    const PlayerStore &players; const AreaStore &areas; skills::System &skills;
    transactions::System &transactions; missiles::System &missiles; combat::System &combat; monsters::System &monsters; EventOutbox &events;
};
class System {
    State state_;
    const Ports ports_;
    std::deque<Reaction> reactions_;
    std::map<EntityId,UnitEffect> units_;
    StepStatus advanceSkills(const ActorContext &, Recovery &);
    StepStatus advanceReactions(uint64_t);
    StepStatus advanceUnits(uint64_t);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<PotionPlan> potion(const ActorContext &, const PotionDefinition &) const;
    void commit(PotionPlan &&plan) noexcept { state_.players.swap(plan.next.players); }
    DomainResult<> apply(const ActorContext &, CombatEffectSpec, bool restoreStamina = false);
    DomainResult<> skill(const ActorContext &, const SkillCastSpec &, std::optional<PlayerId> recipient = {});
    DomainResult<> skillUnit(const ActorContext &, const SkillCastSpec &, EntityId);
    DomainResult<> amazonMagic(const ActorContext &, const SkillCastSpec &);
    CharacterModifiers unitModifiers(EntityId, uint64_t tick) const;
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    std::set<int> unitStates(EntityId, uint64_t tick) const;
    std::map<int,std::vector<std::pair<int,int64_t>>> unitStateStats(EntityId, uint64_t tick) const;
    DomainResult<float> receive(const ActorContext &, int64_t rawDamage, DamageType);
    bool reactionCapacity() const { return reactions_.size() <= 4096-128; }
    void react(const ActorContext &, EntityId attacker, CombatEffectEvent, bool returnFire = true);
    StepStatus step(TickContext, FrameFacts &);
};
}
