#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/units/restoration.hpp"
namespace d2x::server::effects {
struct Recovery { std::deque<ResourceRestoration> healing, mana; CombatEffectSet states; };
struct State { std::map<EntityId, Recovery> players; };
// Prepared before inventory consumption, installed without allocation only after
// the character transaction succeeds. No callback can observe half a drink.
struct PotionPlan { State next; CharacterRecord character; TransientAttributes transient; };
struct Ports {
    const PlayerStore &players; const AreaStore &areas; const skills::System &skills;
    transactions::System &transactions;
};
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<PotionPlan> potion(const ActorContext &, const PotionDefinition &) const;
    void commit(PotionPlan &&plan) noexcept { state_.players.swap(plan.next.players); }
    DomainResult<> apply(const ActorContext &, CombatEffectSpec, bool restoreStamina = false);
    StepStatus step(TickContext, FrameFacts &);
};
}
