#include "system.hpp"
#include "runtime.hpp"
#include "druid_melee.hpp"
#include "native_caster.hpp"
#include "shape.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::skills {
DomainStatus System::allocateDruidMelee(Release &pending,const ActorContext &actor,const SkillCastSpec &skill,bool held) {
    auto &execution=*pending.druidMelee;
    if(!execution.occurrence) {
        if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return DomainStatus::Capacity;
        const uint64_t occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);
        auto contact=std::make_shared<combat::NativeSkillContact>();
        if(execution.monsterProgram) contact->monsterProgram=*execution.monsterProgram;
        if(!skill.druid || skill.druid->program==DruidProgram::Rabies) contact->sourceDamage=128;
        missiles::Spawn request{actor,skill,{},pending.target.position,true};request.nativeMeleeContact=contact;
        request.nativeMeleeOccurrence=occurrence;request.nativeMeleeHeld=held;
        WeaponSkillDamage weapon;weapon.weapon=*pending.weapon;
        const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);if(!caster) return DomainStatus::InvalidActor;
        weapon.level=caster->level();request.weapon=std::move(weapon);
        const auto queued=ports_.missiles.directContact(request,{pending.unit});if(!queued) {--runtime_->nativeKickOccurrence;return queued.status;}
        execution.occurrence=occurrence;execution.contact=std::move(contact);
    }
    pending.nativeCallbackStarted=!held;
    const auto advanced=held?ports_.combat.allocateNativeMelee(actor.actor,*execution.occurrence,{actor.tick}):ports_.combat.advanceAuraPulse(actor.actor,*execution.occurrence,{actor.tick});
    if(advanced==StepStatus::Blocked) return DomainStatus::Capacity;
    if(execution.contact->cancelled || (held && !execution.contact->heldPrepared)) return DomainStatus::Unavailable;
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
DomainResult<> System::startDruidMelee(Release &pending,const ActorContext &actor,const MonsterAttackRule *program) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.skill.druid || !pending.weapon) return {DomainStatus::InvalidActor,{}};
    auto old=runtime_->druidStarts.find(actor.actor);
    if(old!=runtime_->druidStarts.end() && (old->second.actor.sequence!=actor.sequence || old->second.skill.sourceId!=pending.skill.sourceId || old->second.unit!=pending.unit)) {
        if(old->second.druidMelee && old->second.druidMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*old->second.druidMelee->occurrence);
        runtime_->druidStarts.erase(old);
    }
    auto [entry,created]=runtime_->druidStarts.try_emplace(actor.actor,pending);
    if(created) entry->second.druidMelee=std::make_shared<DruidMeleeExecution>();
    pending.druidMelee=entry->second.druidMelee;auto &execution=*pending.druidMelee;
    if(program && !execution.monsterProgram) execution.monsterProgram=std::make_shared<const MonsterAttackRule>(*program);
    if(execution.startPrepared) return {DomainStatus::Applied,std::monostate{}};
    if(pending.skill.druid->program==DruidProgram::Rabies) {
        if(execution.monsterProgram && !execution.accuracy) {
            execution.accuracy=std::make_shared<combat::NativeSkillContact>();
            execution.accuracy->monsterProgram=*execution.monsterProgram;
        }
        const auto sampled=ports_.combat.sampleNativeSkillAccuracy(actor,pending.skill,*pending.weapon,pending.unit,execution.accuracy);
        if(!sampled) return {sampled.status,{}};
        if(!*sampled.value) {runtime_->druidStarts.erase(actor.actor);return {DomainStatus::Unavailable,{}};}
        // St57 evaluates these even though the result is discarded.
        auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        if(!values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1") ||
           !values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc4")) throw std::logic_error("Unavailable native Rabies start formula");
        execution.initialHit=true;
    } else {
        const auto allocated=allocateDruidMelee(pending,actor,pending.skill,true);if(allocated!=DomainStatus::Applied) return {allocated,{}};
        execution.initialHit=execution.contact->hit;
    }
    execution.startPrepared=true;return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::activateDruidMelee(Release &pending,const ActorContext &actor,Vec) {
    if(!pending.druidMelee || !pending.druidMelee->startPrepared) return DomainStatus::Unavailable;
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    auto &execution=*pending.druidMelee;const auto &rules=*caster->programs();const auto role=pending.skill.druid->program;
    const auto drain=[&] {
        pending.nativeCallbackStarted=true;
        if(ports_.combat.drainNativeMelee(actor.actor,*execution.occurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        pending.nativeCallbackStarted=false;execution.occurrence.reset();execution.contact.reset();return DomainStatus::Applied;
    };
    if(execution.phase==DruidMeleeExecution::Phase::Initial) {
        if(role==DruidProgram::Rabies && !execution.occurrence) {
            auto skill=pending.skill;skill.druid.reset();skill.weapon->damagePercent=skill.weapon->attackRating=0;
            const auto *states=ports_.effects.unitEffects(actor.actor);int toHit=0;
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
            for(const auto &[state,shape]:rules.shapes) if(states && states->hasState(state,actor.tick)) {
                const auto *list=states->findState(state);if(!list) continue;
                const auto stats=combatEffectNativeValues(list->spec);if(!stats) continue;
                const int source=StatSnapshot{*stats}.get({rules.stats->names.at("modifierlist_skill"),0});
                const auto hit=values.parameter(FormulaDomain::Skill,source,pending.skill.rank,"toht");if(!hit) throw std::logic_error("Unavailable native transformed Rabies attack");
                toHit=nativeAdd(toHit,*hit);skill.sourceId=source;
            }
            skill.weapon->attackRating=toHit;skill.hitClass=1;
            const auto allocated=allocateDruidMelee(pending,actor,skill,true);if(allocated!=DomainStatus::Applied) return allocated;
        }
        const auto drained=drain();if(drained!=DomainStatus::Applied) return drained;
        execution.phase=role==DruidProgram::Rabies?DruidMeleeExecution::Phase::RabiesInfection:
            role==DruidProgram::Rage && execution.initialHit?DruidMeleeExecution::Phase::RageBuff:DruidMeleeExecution::Phase::Complete;
    }
    if(execution.phase==DruidMeleeExecution::Phase::RageBuff) {
        const auto applied=ports_.effects.druidRage(actor,pending.skill);if(!applied) return applied.status;
        execution.phase=DruidMeleeExecution::Phase::Complete;
    }
    if(execution.phase==DruidMeleeExecution::Phase::RabiesInfection) {
        if(!execution.infectionPosted) {
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
            const auto length=values.parameter(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"edln");if(!length) throw std::logic_error("Unavailable native Rabies duration");
            execution.infectionLength=*length;
            const auto *states=ports_.effects.unitEffects(pending.unit);
            if(states && states->findState(pending.skill.druid->targetState.id)) execution.carrierComplete=true;
            else {
                const auto infected=ports_.effects.skillAuraState(actor.actor,pending.unit,pending.skill.sourceId,pending.skill.rank,*length,actor.tick,effects::NativeSkillStateProcedure::Infection);
                if(!infected) return infected.status;
            }
            execution.infectionPosted=true;
        }
        if(!execution.carrierComplete) {
            const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(pending.unit);
            if(!target.id()) return DomainStatus::Unavailable;
            missiles::NativeSpawn spawn{pending.unit,actor.area,actor.areaGeneration,actor.tick,target.position(),target.position(),pending.skill.missileId,pending.skill.sourceId,pending.skill.rank};
            spawn.totalFrames=execution.infectionLength;spawn.finalParameters=std::array{caster->player?0:1,nativeNarrow(uint32_t(actor.actor.value))};
            const auto born=ports_.missiles.spawnNative(spawn);if(!born) return born.status;
            execution.carrierComplete=true;
        }
        execution.phase=DruidMeleeExecution::Phase::RabiesFinal;
    }
    if(execution.phase==DruidMeleeExecution::Phase::RabiesFinal) {
        const auto applied=allocateDruidMelee(pending,actor,pending.skill,false);if(applied!=DomainStatus::Applied) return applied;
        execution.occurrence.reset();execution.contact.reset();execution.phase=DruidMeleeExecution::Phase::Complete;
    }
    return DomainStatus::Applied;
}
}
