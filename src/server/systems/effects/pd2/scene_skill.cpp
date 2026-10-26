#include "../system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <stdexcept>

namespace d2x::server::effects {
DomainResult<> System::pd2SceneSkillState(EntityId source,EntityId target,int skill,int rank,uint64_t tick) {
    const auto *owner=ports_.monsters.find(source);
    if(!owner || !owner->rule.formulas || !owner->rule.stats || rank<1) return {DomainStatus::InvalidActor,{}};
    const auto &catalog=*owner->rule.formulas;const auto row=catalog.skills.find(skill);
    if(row==catalog.skills.end()) return {DomainStatus::Unavailable,{}};
    const auto values=unitStatValues(source,tick);if(!values) return {DomainStatus::Unavailable,{}};
    auto input=skills::formulaInput(*owner,*values);SkillFormulaValues formula(catalog,input);
    const auto duration=formula.calculation(FormulaDomain::Skill,skill,rank,"auralencalc");
    if(!duration) throw std::logic_error("Missing PD2 scene state duration");
    NativeSkillStateValues state;state.state=row->second.auraTargetState;
    const auto property=owner->rule.stats->names.find(row->second.reference("aurastat1"));
    if(property!=owner->rule.stats->names.end()) {
        const auto amount=formula.calculation(FormulaDomain::Skill,skill,rank,"aurastatcalc1");
        if(!amount) throw std::logic_error("Missing PD2 scene primary state formula");
        state.stats[0]={property->second,*amount};
    }
    // 36D940 posts the supplied one-stat descriptor. In particular, the
    // death485 call does not evaluate the other five Skills aura slots.
    return skillAuraState(source,target,skill,rank,*duration,tick,NativeSkillStateProcedure::Warcry,&state);
}
}
