#include "../system.hpp"
#include "server/systems/monsters/system.hpp"

namespace d2x::server::effects {
DomainResult<> System::pd2LucionPhaseState(EntityId id,bool active,uint64_t tick) {
    const auto *body=ports_.monsters.find(id);
    if(!body || !body->rule.stats || !body->rule.states || !body->rule.states->contains(146)) return {DomainStatus::InvalidActor,{}};
    const auto *states=unitEffects(id);
    if(!active) return removeUnitState(id,146,tick);
    if(states && states->findState(146)) return {DomainStatus::Applied,std::monostate{}};
    // Current 380950 allocates a permanent flag-8 list, state146/stat89=-100.
    UnitDamageStates change;CombatEffectSpec spec;spec.state=body->rule.states->at(146);
    spec.source={CombatEffectSource::Environment,id,146,0};change.status=std::move(spec);
    change.statusWrites.emplace_back(StatKey{89,0},-100);
    auto plan=prepareDamageStates(*body,std::move(change),tick);
    if(!validateAuraUnits(plan)) return {DomainStatus::Stale,{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body->area},plan.facts});
    if(!sent) return {sent.status,{}};
    publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
}
