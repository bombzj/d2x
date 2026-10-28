#include "equipped_skills.hpp"
#include "../system.hpp"
#include "../attribute_projection.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/skills/system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include <stdexcept>

namespace d2x::server::effects {
void applyPd2EquippedBuff(CombatEffectSet &states,const ActorContext &actor,const SkillAuraChange &change,
    const SkillRules &rules,const CombatStateRules &stateRules,const attributes::AuraSourceObserver &observe,std::vector<DomainFact> &facts,bool monster) {
    if(!change.formula || !rules.definitions.contains(change.skill)) throw std::runtime_error("Missing PD2 equipped skill definition");
    const auto context=change.formula->input(*rules.stats);
    SkillFormulaValues values(*rules.formulas,context);
    auto skill=resolveSkill(rules.definitions.at(change.skill).spec,{change.rank,context.baseRanks,0,0,0,0,&values});
    if(!skill.appliedEffect) throw std::runtime_error("PD2 equipped skill requires a forced periodic program");
    auto effect=*skill.appliedEffect;effect.source={CombatEffectSource::Skill,actor.actor,change.skill,change.rank};
    const auto observation=[&] {
        auto projection=projectEffectAttributes(states,actor.tick);
        observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),{}});
    };
    const auto result=applySkillState(states,skill,effect,*rules.stats,stateRules,actor.tick,observation);
    const auto type=uint8_t(monster?1:0);
    for(const auto state:result.clearedStates) if(!states.hasState(state,actor.tick)) facts.emplace_back(StateFact{actor.actor,type,actor.area,state,false});
    for(const auto &removed:result.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
        facts.emplace_back(StateFact{actor.actor,type,actor.area,removed.effect.spec.state.id,false});
    if(result.accepted) {
        const auto *installed=states.find(result.handle);if(!installed) throw std::logic_error("Missing PD2 equipped skill state");
        facts.emplace_back(StateFact{actor.actor,type,actor.area,installed->spec.state.id,true,nativeEffectStats(rules,installed->spec)});
    }
}
StepStatus System::advancePd2EquippedSkill(const ActorContext &actor,PaladinCycle &cycle,uint64_t tick) {
    if(cycle.pulseCommitted) return StepStatus::Complete;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !caster->programs()->definitions.contains(cycle.skill)) return StepStatus::Complete;
    // The core skips the forced equipment program during Charge/Whirlwind;
    // its owner keeps the selected and used skill throughout the callback.
    const auto used=ports_.actions.usedSkill(actor.actor);
    if(used && (used->skill==107 || used->skill==415)) {cycle.pulseCommitted=true;return StepStatus::Complete;}
    if(cycle.skill==593 && (ports_.actions.casts.contains(actor.actor) || ports_.actions.recovering(actor.actor,tick)))
        {cycle.pulseCommitted=true;return StepStatus::Complete;}
    auto skill=skills::evaluateNativeSkill(*caster,*this,cycle.skill,cycle.rank,tick);
    const auto &definition=caster->programs()->definitions.at(cycle.skill);
    DomainResult<> result;
    if(skill.pd2 && skill.pd2->doFunction==29) {
        if(!cycle.equippedStorm) cycle.equippedStorm.emplace();
        result=advancePd2EquippedStorm(actor,skill,*cycle.equippedStorm);
    } else if(skill.pd2 && skill.pd2->doFunction==54)
        result=bladeShield(actor,skill,cycle.equippedBladeShield);
    else result=ports_.skills.itemTrigger(actor,std::move(skill),definition.collision,actor.actor,caster->position(),false,0,&cycle.equippedRelease);
    if(isDeferred(result.status)) return StepStatus::Blocked;
    if(!result) state_.itemAuraDeferred="PD2 equipped skill program is unavailable: "+std::to_string(cycle.skill);
    cycle.pulseCommitted=true;return StepStatus::Complete;
}
}
