#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
#include "native_caster.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/mind_blast.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace d2x::server::skills {
DomainResult<bool> System::startBladeFury(Release &pending,const ActorContext &actor,std::span<const DomainFact> facts) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    const int state=caster->programs()->formulas->infernoState;
    const auto states=ports_.effects.unitEffects(caster->id());const bool warm=states && states->findState(state);
    auto cast=pending.skill;CombatEffectSpec effect;
    effect.state=caster->player?caster->player->rules.character->states->at(state):caster->monster->rule.states->at(state);
    effect.source={CombatEffectSource::Skill,caster->id(),cast.sourceId,cast.rank};
    effect.duration=warm?7:21;effect.nativeState=effect.nativeDirectAllocation=true;effect.nativeValues.emplace();
    effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;effect.stacking=EffectStacking::ReuseState;
    cast.appliedEffect=std::move(effect);cast.nativeEffectWrites.clear();cast.manaCost=cast.startMana=0;cast.charge.reset();
    const auto installed=caster->monster?ports_.effects.unitSkill(caster->id(),cast,actor.tick):ports_.effects.skill(actor,cast,{},facts);
    if(!installed) return {installed.status,{}};
    if(!warm) state_.skillParameter(caster->id(),{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}},0,0);
    return {DomainStatus::Applied,warm};
}
DomainStatus System::activateAssassin(Release &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.skill.assassin) return DomainStatus::InvalidActor;
    const auto *player=caster->controller;const auto &rules=*caster->programs();
    const auto &program=*pending.skill.assassin;
    if(program.program==AssassinProgram::DragonTalon || program.program==AssassinProgram::DragonTail || program.program==AssassinProgram::DragonFlight)
        return activateAssassinKick(pending,actor,target);
    if(program.program==AssassinProgram::Sentry || program.program==AssassinProgram::BladeSentinel || program.program==AssassinProgram::Shadow) {
        auto skill=pending.skill;if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        return ports_.companions.assassin(actor,skill,target).status;
    }
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
        if(!result) throw std::runtime_error("Unavailable native assassin formula");
        return *result;
    };
    if(program.program==AssassinProgram::BladeFury) {
        const auto duration=nativeSubtract(formula("prgcalc1"),1);if(duration<=0) return DomainStatus::Unavailable;
        const NativeSkillSlot slot{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}};
        if(state_.skillParameter(caster->id(),slot,0)>=nativeNarrow(uint32_t(actor.tick))) return DomainStatus::Applied;
        missiles::NativeSpawn spawn{caster->id(),actor.area,actor.areaGeneration,actor.tick,caster->position(),target,
            pending.skill.missileId,pending.skill.sourceId,pending.skill.rank};
        auto prepared=ports_.missiles.prepareNativeBirth(std::span{&spawn,size_t{1}});if(!prepared) return prepared.status;
        DomainStatus status;
        if(caster->player) {
            if(player->persistent.player.mana<pending.skill.manaCost) return DomainStatus::Unavailable;
            transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
            edit.player.mana-=pending.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};edit.charge=pending.skill.charge;
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return plan.status;
            plan.value->missileBirth=std::move(*prepared.value);status=ports_.transactions.commit(std::move(*plan.value)).status;
            if(status==DomainStatus::Applied && pending.skill.charge) pending.skill.charge->item=player->persistent.inventory.items.at(pending.skill.charge->item.id).handle();
        } else status=ports_.missiles.commitNativeBirth(std::move(*prepared.value)).status;
        if(status==DomainStatus::Applied) state_.skillParameter(caster->id(),slot,0,nativeAdd(nativeNarrow(uint32_t(actor.tick)),duration));
        return status;
    }
    if(program.program==AssassinProgram::FireBlast || program.program==AssassinProgram::ShockWeb) {
        const bool shock=program.program==AssassinProgram::ShockWeb;
        const int count=shock?std::max(formula("prgcalc1"),1):1;
        if(count>512) return DomainStatus::Capacity;
        const auto radius=shock?formula("aurarangecalc"):0;
        std::vector<missiles::NativeSpawn> births;births.reserve(size_t(count));
        for(int index=0;index<count;++index) {
            missiles::NativeSpawn spawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,
                caster->position(),target,pending.skill.missileId,pending.skill.sourceId,pending.skill.rank};
            if(shock) {
                spawn.distanceCountdown=true;
                if(!index) spawn.ownerLowSeed=uint32_t(int32_t(target.x+ports_.areas.at(actor.area).definition.origin.x));
                if(count>1 && radius>=2) {spawn.scatterRadius=radius;spawn.minimumTargetDistanceSquared=4;}
            }
            births.push_back(spawn);
        }
        auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return prepared.status;
        if(caster->monster) return ports_.missiles.commitNativeBirth(std::move(*prepared.value)).status;
        transactions::CharacterEdit payment{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
        if(!pending.manaPaid) {payment.player.mana-=pending.skill.manaCost;payment.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};payment.charge=pending.skill.charge;}
        auto plan=ports_.transactions.prepare(std::move(payment));if(!plan) return plan.status;
        plan.value->missileBirth=std::move(*prepared.value);
        return ports_.transactions.commit(std::move(*plan.value)).status;
    }
    if(program.program==AssassinProgram::PsychicHammer) {
        if(pending.nativeMeleeOccurrence) {
            if(ports_.combat.advanceAuraPulse(actor.actor,*pending.nativeMeleeOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
            pending.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;return DomainStatus::Applied;
        }
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto recipient=participants.find(pending.unit);
        if(!recipient.aliveIn(actor.area) || !participants.canHarm(actor.actor,pending.unit) || ports_.areas.at(actor.area).definition.town)
            return DomainStatus::Unavailable;
        combat::SpellImpact impact;impact.projectile=impact.source=actor.actor;impact.area=actor.area;
        impact.occurrence=actor.sequence?actor.sequence:actor.tick+1;impact.targets={pending.unit};
        impact.nativeDamageFlagsOnly=impact.synchronous=true;
        impact.missileDamage=std::make_shared<MissileDamageRollState>();
        impact.missileDamage->formulas=rules.formulas;impact.missileDamage->statRules=rules.stats;
        impact.missileDamage->skill=pending.skill.sourceId;impact.missileDamage->level=pending.skill.rank;
        impact.missileDamage->uninterruptableState=rules.uninterruptable.id;
        impact.missileDamage->lifeStealDivisor=rules.lifeStealDivisor;
        impact.missileDamage->manaStealDivisor=rules.manaStealDivisor;
        impact.unblockable=true;
        impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->resultFlags=1;
        impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();
        impact.nativeSkill=std::make_shared<combat::NativeSkillContact>();
        impact.nativeSkill->skill=pending.skill.sourceId;impact.nativeSkill->rank=pending.skill.rank;impact.nativeSkill->psychicHammer=true;
        impact.monsterStates={rules.chilled,rules.poisoned,{}};
        const auto occurrence=impact.occurrence;auto queued=ports_.combat.prepareSpells({std::move(impact)});if(!queued) return queued.status;
        if(caster->player) {
            const auto debit=ports_.transactions.release(actor,player->characterRevision,pending.manaPaid?Fixed8{}:pending.skill.manaCost,{},pending.manaPaid?std::optional<SkillCharge>{}:pending.skill.charge,{},{},pending.manaPaid?-1:pending.skill.sourceId);
            if(!debit) return debit.status;
        }
        ports_.combat.commitSpells(std::move(*queued.value));pending.manaPaid=true;
        pending.nativeMeleeOccurrence=occurrence;pending.nativeCallbackStarted=true;
        if(ports_.combat.advanceAuraPulse(actor.actor,occurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        pending.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;return DomainStatus::Applied;
    }
    if(program.program==AssassinProgram::Cloak) {
        if(ports_.effects.unitStates(caster->id(),actor.tick).contains(program.state.id)) return DomainStatus::Unavailable;
        auto skill=pending.skill;
        const auto &stats=*rules.stats;
        CombatEffectSpec self;self.state=program.state;self.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
        self.duration=uint32_t(formula("auralencalc"));self.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
        self.nativeState=true;self.nativeValues.emplace();self.stacking=EffectStacking::CurseLevel;
        self.refreshPassivesOnRemoval=true;
        skill.nativeEffectWrites=values.passiveBuffStats(skill.sourceId,skill.rank,false);
        skill.nativeEffectWrites.emplace_back(StatKey{stats.names.at("modifierlist_skill"),0},skill.sourceId);
        skill.nativeEffectWrites.emplace_back(StatKey{stats.names.at("modifierlist_level"),0},skill.rank);
        skill.appliedEffect=std::move(self);
        CurseSpec curse;curse.targetProcedure=CurseSpec::TargetProcedure::Cloak;curse.state=program.targetState;
        curse.ai=CurseAi::DimVision;curse.ignoreDifficultyDivisor=true;curse.writeZeroSecondary=true;
        curse.targetFilter=rules.formulas->skills.at(skill.sourceId).number("aurafilter");
        curse.radius=formula("aurarangecalc");curse.frames=formula("auralencalc");curse.nativeValues.emplace();
        curse.nativeWrites=values.auraStats(skill.sourceId,skill.rank,6,false);
        curse.primaryStat=values.auraStatKey(skill.sourceId,1);
        if(curse.primaryStat && !curse.primaryStat->id) curse.primaryStat.reset();
        skill.curse=std::move(curse);
        if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        return ports_.effects.curse(actor,skill,caster->position(),{}).status;
    }
    if(program.program==AssassinProgram::BladeShield) {
        if(!pending.manaPaid) {
            const auto result=caster->monster?ports_.effects.unitSkill(caster->id(),pending.skill,actor.tick):ports_.effects.skill(actor,pending.skill);if(!result) return result.status;
            pending.manaPaid=true;
        }
        const auto result=ports_.effects.bladeShield(actor,pending.skill,pending.bladeShieldPulse);
        pending.nativeCallbackStarted=ports_.effects.bladeShieldStarted(pending.bladeShieldPulse);return result.status;
    }
    if(program.program==AssassinProgram::MindBlast) {
        auto skill=pending.skill;if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        const auto result=ports_.effects.mindBlast(actor,skill,target,pending.mindBlastPulse);
        if(pending.mindBlastPulse && pending.mindBlastPulse->prepared) {
            pending.manaPaid=pending.manaPaid || pending.mindBlastPulse->manaPaid;
            pending.nativeCallbackStarted=!pending.mindBlastPulse->complete;
            pending.target.position=pending.mindBlastPulse->center;pending.unit={};
        }
        return result.status;
    }
    return DomainStatus::NotImplemented;
}
}
