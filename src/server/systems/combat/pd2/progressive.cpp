#include "progressive.hpp"
#include "../system.hpp"
#include "../progressive_execution.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::combat {
bool pd2Gathering(const SpellImpact &impact) {
    return impact.nativeSkill && impact.weapon && impact.weapon->wearSkill.pd2 &&
        impact.weapon->wearSkill.pd2->doFunction==170;
}
DomainStatus preparePd2Gather(const Ports &ports,SpellImpact &impact,TickContext tick) {
    if(!pd2Gathering(impact)) return DomainStatus::Applied;
    auto &release=impact.nativeSkill->release;
    if(!release) release=std::make_shared<ProgressiveContactExecution>();
    if(!release->gather) release->gather.emplace();
    auto &gather=*release->gather;
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,impact.source);
    if(!caster) return DomainStatus::Stale;
    if(!gather.clearPrepared) {
        const auto charges=ports.effects.progressiveCharges(impact.source);
        if(std::any_of(charges.begin(),charges.end(),[](const auto &charge){return charge.count>=3;}))
            for(const auto &charge:charges) gather.clearStates.push_back(charge.state);
        gather.clearPrepared=true;
    }
    while(gather.clearNext<gather.clearStates.size()) {
        const auto result=ports.effects.removeState(caster->context(ports.areas,tick.tick),gather.clearStates[gather.clearNext]);
        if(!result) return result.status;
        ++gather.clearNext;
    }
    return DomainStatus::Applied;
}
DomainStatus postPd2Gather(const Ports &ports,SpellImpact &impact,TickContext tick) {
    if(!pd2Gathering(impact) || !impact.nativeSkill->hit) return DomainStatus::Applied;
    auto &gather=*impact.nativeSkill->release->gather;
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,impact.source);
    if(!caster) return DomainStatus::Stale;
    const auto actor=caster->context(ports.areas,tick.tick);const auto &skill=impact.weapon->wearSkill;
    if(!gather.chargePrepared) {
        for(const auto &charge:ports.effects.progressiveCharges(impact.source))
            if(charge.skill==skill.sourceId) gather.oldCount=charge.count;
        const auto result=ports.effects.progressiveCharge(actor,skill);if(!result) return result.status;
        for(const auto &charge:ports.effects.progressiveCharges(impact.source))
            if(charge.skill==skill.sourceId) gather.chargeCount=charge.count;
        gather.chargePrepared=true;impact.nativeSkill->chargePosted=true;
    }
    if(!gather.cobraPrepared) {
        // Do170 gives Cobra's second stage an independent three-frame
        // list before FillDamageValues. It is not a persistent charge bonus.
        if(skill.assassin->chargeStat==319 && gather.oldCount<2 && gather.chargeCount>=2) {
            auto input=caster->formulaInput(ports.effects,tick.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
            const auto leech=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc1");
            const auto damage=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc2");
            if(!leech || !damage) throw std::logic_error("Missing PD2 Cobra contact formulas");
            auto buff=skill;CombatEffectSpec effect;effect.duration=3;effect.nativeDirectAllocation=true;effect.stacking=EffectStacking::Independent;
            effect.nativeValues.emplace();effect.source={CombatEffectSource::Skill,impact.source,skill.sourceId,skill.rank};
            const auto &stats=*caster->programs()->stats;
            buff.nativeEffectWrites={{{stats.names.at("lifedrainmindam"),0},*leech},
                {{stats.names.at("manadrainmindam"),0},*leech},{{stats.names.at("damagepercent"),0},*damage}};
            buff.appliedEffect=std::move(effect);buff.manaCost=buff.startMana=0;buff.charge.reset();
            const auto result=caster->monster?ports.effects.unitSkill(impact.source,buff,tick.tick):ports.effects.skill(actor,buff);
            if(!result) return result.status;
        }
        gather.cobraPrepared=true;
    }
    return DomainStatus::Applied;
}
}
