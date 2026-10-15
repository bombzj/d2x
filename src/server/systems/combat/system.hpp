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

namespace d2x::server::combat {
struct State { std::list<Damage> pending; std::list<SpellImpact> spells; size_t spellTargets{}; uint32_t hitClassCursor{}; uint64_t targetAttempts{}, reflectionOccurrence{}; };
struct Ports { const PlayerStore &players; monsters::System &monsters; const AreaStore &areas; transactions::System &transactions; uint64_t &random; EventOutbox &events; effects::System &effects; inventory::System &inventory; const social::System &social; reactions::ItemTriggers &itemTriggers; };
class System {
    State state_;
    const Ports ports_;
    StepStatus resolveSpells(TickContext,std::optional<std::pair<EntityId,uint64_t>> selected={});
    DomainStatus resolveEnvironment(SpellImpact &,TickContext);
    DomainStatus prepareMissileContact(SpellImpact &,ParticipantView,ParticipantView,uint64_t generation,TickContext,bool preludeOnly=false);
    DomainStatus finishMissileArmor(SpellImpact &,ParticipantView,TickContext);
    DomainStatus advanceMissileDrain(SpellImpact &,ParticipantView,int64_t physical,int64_t life,int64_t mana,int64_t stamina,TickContext);
    DomainStatus resolvePlayerImpact(SpellImpact &,ParticipantView,const PlayerState &,const PlayerState &,uint64_t generation,TickContext);
    DomainStatus finishPlayerContact(SpellImpact &,const PlayerState &,TickContext);
    DomainStatus finishMonsterContact(SpellImpact &,const monsters::Actor &,TickContext);
    DomainStatus advanceMeleeAttempts(SpellImpact &,TickContext);
    DomainStatus finishImpact(SpellImpact &, const PlayerState *, uint64_t generation, uint64_t tick);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> enqueue(const Damage &);
    DomainResult<MissilePreludeResult> missilePrelude(SpellImpact &,TickContext);
    bool weaponContact(const WeaponSkillDamage &,EntityId,uint64_t,uint64_t &) const;
    DomainResult<> enqueue(SpellImpact);
    DomainResult<SpellPlan> prepareSpells(std::vector<SpellImpact>) const;
    void commitSpells(SpellPlan &&) noexcept;
    StepStatus advanceAuraPulse(EntityId source,uint64_t occurrence,TickContext tick) {return resolveSpells(tick,std::pair{source,occurrence});}
    void cancel(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
