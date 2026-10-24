#include "system.hpp"
#include "mind_blast.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>

namespace d2x::server::effects {
DomainResult<> System::mindBlast(const ActorContext &actor,const SkillCastSpec &skill,Vec center,std::shared_ptr<MindBlastPulse> &retained) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!retained) {retained=std::make_shared<MindBlastPulse>();retained->actor=actor;retained->skill=skill;retained->center=center;}
    auto &pulse=*retained;if(pulse.complete) return done();
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pulse.skill.assassin) return {DomainStatus::InvalidActor,{}};
    const auto &rules=*caster->programs();
    const auto consume=[&](uint64_t expected,uint64_t next) {
        return caster->monster?ports_.monsters.consumeRandom(caster->id(),expected,next):ports_.transactions.consumeRandom(actor,expected,next);
    };
    if(!pulse.prepared) {
        const auto &area=ports_.areas.at(actor.area);if(area.definition.town) return {DomainStatus::Unavailable,{}};
        const auto expected=caster->random();auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=caster->formulaInput(*this,actor.tick,draw);SkillFormulaValues values(*rules.formulas,input);
        auto radius=pulse.skill.pd2?values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"aurarangecalc"):
            values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"prgcalc1");
        if(!radius) throw std::logic_error("Unavailable native Mind Blast progressive range");
        if(!*radius) radius=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"aurarangecalc");
        if(!radius) throw std::logic_error("Unavailable native Mind Blast range");
        auto base=std::make_shared<const MissileHitDamage>(rollNativeSkillDamage(values,pulse.skill.sourceId,pulse.skill.rank,random));
        const auto &native=rules.formulas->skills.at(pulse.skill.sourceId);
        if(pulse.skill.pd2) {
            // Current Do51 (RVA 0x3AD370) retains an owner-side stack list.
            // Both radius and expiration use max(oldCount,1), before the
            // count is incremented. PD2 does not run retail conversion.
            const auto key=values.auraStatKey(pulse.skill.sourceId,1);
            const auto duration=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"auralencalc");
            const auto maximum=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"calc2");
            const auto increment=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"calc1");
            if(!key || !duration || !maximum || !increment) throw std::logic_error("Missing PD2 Mind Blast stack inputs");
            int count=0;
            if(const auto *states=unitEffects(caster->id())) if(const auto *old=states->findState(pulse.skill.assassin->state.id))
                if(old->spec.nativeValues) if(const auto it=old->spec.nativeValues->find(*key);it!=old->spec.nativeValues->end()) count=it->second;
            const auto previous=std::max(count,1);
            *radius=nativeAdd(*radius,nativeMultiply(previous-1,*increment));
            CombatEffectSpec effect;effect.state=pulse.skill.assassin->state;effect.nativeState=effect.nativeDirectAllocation=true;
            effect.nativeValues.emplace();effect.duration=uint32_t(nativeMultiply(previous,*duration));
            effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;effect.stacking=EffectStacking::ReuseState;
            effect.source={CombatEffectSource::Skill,caster->id(),pulse.skill.sourceId,pulse.skill.rank};
            pulse.skill.appliedEffect=std::move(effect);pulse.skill.nativeEffectWrites={{*key,count<*maximum?nativeAdd(count,1):count}};
        }
        auto targets=nativeAuraCandidateRecipients({caster->id(),actor.area,*radius,pulse.skill.pd2?0x8583u:uint32_t(native.number("aurafilter")),false,
            pulse.center+area.definition.origin,NativeAuraTargetMode::TargetRange,actor.tick});
        if(caster->player) {
            const auto *player=caster->player;
            transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
            edit.player.mana-=pulse.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{pulse.skill.manaCost,pulse.skill.sourceId};edit.charge=pulse.skill.charge;edit.random=transactions::UnitRandomEdit{expected,random};
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
            const auto paid=ports_.transactions.commit(std::move(*plan.value));if(!paid) return paid;
        } else {const auto result=consume(expected,random);if(!result) return result;}
        pulse.chance=skillDiminishingBonus({native.number("param5"),native.number("param6")},pulse.skill.rank);
        pulse.durationBase=native.number("param3");pulse.durationRange=native.number("param4");
        pulse.hitFlags=uint32_t(native.number("hitflags"));pulse.resultFlags=uint16_t(native.number("resultflags"));
        pulse.base=std::move(base);pulse.targets=std::move(targets);pulse.prepared=pulse.manaPaid=true;
    }
    if(pulse.skill.pd2 && !pulse.progressivePosted) {
        auto applied=pulse.skill;applied.manaCost=applied.startMana=0;applied.charge.reset();
        const auto result=caster->monster?unitSkill(caster->id(),applied,actor.tick):this->skill(actor,applied);
        if(!result) return result;
        pulse.progressivePosted=true;
    }
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    while(pulse.next<pulse.targets.size()) {
        const auto target=participants.find(pulse.targets[pulse.next]);
        if(!target.id()) {++pulse.next;pulse.phase=MindBlastPulse::Phase::Select;continue;}
        if(pulse.phase==MindBlastPulse::Phase::Select) {
            if(!target.aliveIn(target.player?target.player->area:target.monster->area)) {++pulse.next;continue;}
            const auto *monster=target.monster;
            const bool eligible=!pulse.skill.pd2 && monster && !monster->hireling && monster->alignment()==UnitAlignment::Evil &&
                skillAiControlAllowed(monster->identity.rank,monster->rule.switchAi,monster->rule.boss);
            bool convertTarget=false;
            if(eligible) {
                const auto expected=caster->random();auto random=expected;
                // Retail 1.13c RVA 91610 SETLE, not a strict comparison.
                convertTarget=int32_t(limitedRandom(random,100))<=pulse.chance;
                if(convertTarget) pulse.duration=nativeAdd(pulse.durationBase,int32_t(limitedRandom(random,uint32_t(pulse.durationRange))));
                const auto consumed=consume(expected,random);if(!consumed) return consumed;
            }
            pulse.phase=convertTarget?MindBlastPulse::Phase::Convert:MindBlastPulse::Phase::Damage;
        }
        if(pulse.phase==MindBlastPulse::Phase::Convert) {
            NativeConversionSpec conversion{pulse.skill.assassin->conversionState,pulse.skill.assassin->conversionSaveState,pulse.duration,NativeConversionProcedure::MindBlast};
            const auto converted=convert(actor,target.id(),conversion);if(!converted) return converted;
            ++pulse.next;pulse.phase=MindBlastPulse::Phase::Select;continue;
        }
        if(!pulse.occurrence) {
            if(mindBlastOccurrence_>=(uint64_t{1}<<56)-1) return {DomainStatus::Capacity,{}};
            const auto occurrence=(uint64_t{1}<<58)|(mindBlastOccurrence_+1);
            combat::SpellImpact impact;impact.projectile=impact.source=caster->id();
            impact.area=target.player?target.player->area:target.monster->area;impact.targets={target.id()};impact.occurrence=occurrence;
            impact.nativeAura=impact.synchronous=impact.nativeDamageFlagsOnly=impact.unblockable=true;
            impact.retainDeadSource=true;
            impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->hitFlags=pulse.hitFlags;impact.areaDamage->resultFlags=pulse.resultFlags;
            impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();impact.areaDamage->rolled->value=*pulse.base;
            impact.missileDamage=std::make_shared<MissileDamageRollState>();impact.missileDamage->formulas=rules.formulas;impact.missileDamage->statRules=rules.stats;
            impact.missileDamage->uninterruptableState=rules.uninterruptable.id;
            impact.monsterStates={rules.chilled,rules.poisoned,{}};
            auto prepared=ports_.combat.prepareSpells({std::move(impact)});if(!prepared) return {prepared.status,{}};
            ports_.combat.commitSpells(std::move(*prepared.value));++mindBlastOccurrence_;pulse.occurrence=occurrence;
        }
        if(ports_.combat.advanceAuraPulse(caster->id(),*pulse.occurrence,{actor.tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        pulse.occurrence.reset();++pulse.next;pulse.phase=MindBlastPulse::Phase::Select;
    }
    pulse.complete=true;return done();
}
}
