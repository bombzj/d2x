#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/damage_type.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::combat {
// Hit, mitigation and resource damage planning; death is a separate transition.
struct Damage {
    EntityId source, target; DamageType type; int64_t minimum{}, maximum{}; uint64_t action{};
    RegionId area; uint64_t impact{}; int rating{}, level{}, range{}, sourceSize{2};
    std::optional<WeaponDamage> weapon;
    int criticalChance{}, deadlyChance{};
};
// Targets are captured at missile impact, not looked up again by radius on retry.
struct SpellImpact {
    EntityId projectile, source;
    RegionId area;
    DamageType type{DamageType::Fire};
    int64_t damage{};
    std::vector<EntityId> targets;
    size_t next{};
};
struct State { std::vector<Damage> pending; std::deque<SpellImpact> spells; size_t spellTargets{}; };
struct Ports { const PlayerStore &players; monsters::System &monsters; const AreaStore &areas; transactions::System &transactions; uint64_t &random; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
    StepStatus resolveSpells(TickContext);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> enqueue(const Damage &);
    DomainResult<> enqueue(SpellImpact);
    void cancel(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
