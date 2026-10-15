#include "system.hpp"
#include "gameplay/effects/queries.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::druidRage(const ActorContext &actor,const SkillCastSpec &cast) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !cast.druid) return {DomainStatus::InvalidActor,{}};
    const auto &rules=*caster->programs();const auto &stats=*rules.stats;
    auto input=caster->formulaInput(*this,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto duration=values.calculation(FormulaDomain::Skill,cast.sourceId,cast.rank,"auralencalc");
    const auto limit=values.calculation(FormulaDomain::Skill,cast.sourceId,cast.rank,"calc2");
    if(!duration || !limit) throw std::logic_error("Unavailable native Rage formula");
    const StatKey countStat{stats.names.at("skill_frenzy"),0};int previous=0;
    if(const auto *states=unitEffects(actor.actor)) if(const auto *list=states->findState(cast.druid->state.id))
        if(const auto native=combatEffectNativeValues(list->spec)) previous=StatSnapshot{*native}.get(countStat);
    const auto count=std::min(*limit,nativeAdd(previous,1));
    CombatEffectSpec effect;effect.state=cast.druid->state;effect.source={CombatEffectSource::Skill,actor.actor,cast.sourceId,cast.rank};
    effect.duration=uint32_t(*duration);effect.nativeState=effect.nativeDirectAllocation=effect.refreshPassivesOnRemoval=true;
    effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;effect.stacking=EffectStacking::ReuseState;effect.nativeValues.emplace();
    std::vector<StatValue> writes{{countStat,count},{{stats.names.at("modifierlist_skill"),0},cast.sourceId},{{stats.names.at("modifierlist_level"),0},cast.rank}};
    for(const auto &write:values.auraStats(cast.sourceId,count,6,true)) {
        writes.push_back(write);if(write.key.id==stats.names.at("attackrate")) writes.emplace_back(StatKey{stats.names.at("other_animrate"),0},write.value);
    }
    if(caster->player) return apply(actor,std::move(effect),false,writes);
    auto skill=cast;skill.appliedEffect=std::move(effect);skill.nativeEffectWrites=std::move(writes);
    return unitSkill(actor.actor,skill,actor.tick);
}
}
