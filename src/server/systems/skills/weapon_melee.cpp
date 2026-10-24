#include "system.hpp"
#include "runtime.hpp"
#include "weapon_melee.hpp"
#include "evaluation.hpp"
#include "native_caster.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>
#include <algorithm>

namespace d2x::server::skills {
DomainResult<> System::startWeaponMelee(Release &pending,const ActorContext &actor,const MonsterAttackRule *program) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.weapon) return {DomainStatus::InvalidActor,{}};
    auto old=runtime_->weaponStarts.find(actor.actor);
    if(old!=runtime_->weaponStarts.end() && (old->second.actor.sequence!=actor.sequence || old->second.unit!=pending.unit || old->second.skill.sourceId!=pending.skill.sourceId)) {
        if(old->second.weaponMelee && old->second.weaponMelee->occurrence)
            ports_.combat.discardNativeMelee(actor.actor,*old->second.weaponMelee->occurrence);
        runtime_->weaponStarts.erase(old);
    }
    auto [entry,created]=runtime_->weaponStarts.try_emplace(actor.actor,pending);
    if(created) entry->second.weaponMelee=std::make_shared<WeaponMeleeExecution>();
    pending.weaponMelee=entry->second.weaponMelee;auto &execution=*pending.weaponMelee;
    if(!execution.occurrence) {
        if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return {DomainStatus::Capacity,{}};
        const auto occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);
        auto contact=std::make_shared<combat::NativeSkillContact>();
        if(program) contact->monsterProgram=*program;
        contact->vengeanceCycle=state_.skillParameter(actor.actor,{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}},0);
        missiles::Spawn request{actor,pending.skill,{},pending.target.position,true};
        request.nativeMeleeContact=contact;request.nativeMeleeOccurrence=occurrence;request.nativeMeleeHeld=true;
        WeaponSkillDamage weapon;weapon.weapon=*pending.weapon;weapon.level=caster->level();
        request.weapon=std::move(weapon);
        const auto queued=ports_.missiles.directContact(request,{pending.unit});
        if(!queued) {--runtime_->nativeKickOccurrence;return {queued.status,{}};}
        execution.occurrence=occurrence;execution.contact=std::move(contact);
    }
    if(!execution.allocated) {
        if(ports_.combat.allocateNativeMelee(actor.actor,*execution.occurrence,{actor.tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        if(execution.contact->cancelled || !execution.contact->heldPrepared) {
            ports_.combat.discardNativeMelee(actor.actor,*execution.occurrence);
            runtime_->weaponStarts.erase(actor.actor);
            return {DomainStatus::Unavailable,{}};
        }
        execution.allocated=true;
    }
    // St32 adds calc2 after AllocCombat, hence after reception and conversion.
    // It is not weapon ED and must not be amplified by critical strike.
    if(!execution.postAllocated) {
        if(!program && pending.skill.weapon->usesConcentrateStart() && execution.contact->hit) {
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
            const auto flat=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc2");
            if(!flat) throw std::logic_error("Missing native St32 post-allocation damage");
            if(*flat && !ports_.combat.addAllocatedPhysical(actor.actor,*execution.occurrence,nativeShiftLeft(*flat,8)))
                return {DomainStatus::Stale,{}};
        }
        execution.postAllocated=true;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::drainWeaponMelee(Release &pending,const ActorContext &actor) {
    auto &execution=*pending.weaponMelee;
    const int doFunction=pending.skill.weapon?pending.skill.weapon->doFunction:2;
    if(!execution.allocated || !execution.occurrence || (!execution.drained && execution.contact->cancelled)) return DomainStatus::Unavailable;
    pending.nativeCallbackStarted=true;
    if(doFunction==79) {
        if(!execution.conversion) {
            const auto &player=*ports_.players.find(actor.player);
            const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(pending.unit);
            bool chosen=false;const auto expected=player.combatRandom;auto random=expected;
            if(execution.contact->hit && target.monster && target.monster->alive() && target.monster->rule.convertible &&
                !target.monster->hireling && !nativeUniqueMonster(target.monster->identity.rank) && target.monster->damageable() &&
                !ports_.effects.unitStates(target.id(),actor.tick).contains(target.monster->rule.uninterruptableState) &&
                target.monster->alignment()==UnitAlignment::Evil) {
                auto input=formulaInput(player);SkillFormulaValues values(*player.rules.skills->formulas,input);
                const auto chance=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
                const auto length=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"auralencalc");
                if(!chance || !length) throw std::logic_error("Missing native Conversion formulas");
                chosen=int32_t(limitedRandom(random,100))<*chance;execution.conversionFrames=std::max(1,*length);
            }
            const auto consumed=ports_.transactions.consumeRandom(actor,expected,random);if(!consumed) return consumed.status;
            execution.conversion=chosen;
        }
        if(*execution.conversion) {
            auto program=*pending.skill.weapon;program.conversionFrames=execution.conversionFrames;
            const auto converted=ports_.effects.convert(actor,pending.unit,program);if(!converted) return converted.status;
            // Successful Do79 changes allegiance and returns without Drain.
            ports_.combat.discardNativeMelee(actor.actor,*execution.occurrence);
            execution.occurrence.reset();pending.nativeCallbackStarted=false;return DomainStatus::Applied;
        }
    }
    if(doFunction==64 && !execution.recoilComplete) {
        const auto &player=*ports_.players.find(actor.player);
        const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(pending.unit);
        const auto life=target.player?target.player->persistent.player.hp.raw():target.monster?target.monster->life:0;
        auto input=formulaInput(player);SkillFormulaValues values(*player.rules.skills->formulas,input);
        const auto percent=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc2");
        if(!percent) throw std::logic_error("Missing native Sacrifice recoil percentage");
        const auto base=pending.skill.pd2?player.persistent.player.hp.raw():nativeChecked(std::min<int64_t>(execution.contact->allocatedPhysical,life));
        const auto amount=target.aliveIn(actor.area)?nativePercentage(base,*percent,100):0;
        if(amount>0 && player.persistent.player.hp>0) {
            // Do64 executes its already-computed physical total on itself with
            // HitFlags 0x1000 and no SUCCESSFULHIT: no mitigation or GetHit proc.
            MonsterHit hit;hit.channels[0]=amount;hit.getHit=false;
            DamageReception reception;reception.channels[0].dealt=reception.dealt=amount;
            effects::PlayerHitAdjustment adjustment;adjustment.credit=actor.actor;
            const auto received=ports_.effects.receiveMonster(actor,actor.actor,hit,{}, {},adjustment,&reception,false,false);
            if(!received) return received.status;
        }
        execution.recoilComplete=true;
    }
    if(!execution.drained) {
        if(ports_.combat.drainNativeMelee(actor.actor,*execution.occurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        execution.drained=true;
    }
    if(doFunction==158 && pending.skill.pd2) {
        const auto status=activatePd2Bash(pending,actor);if(status!=DomainStatus::Applied) return status;
    }
    if(doFunction==164 && pending.skill.pd2) {
        const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(pending.unit);
        if(target.id()) {
            const auto result=activatePd2(pending,actor,target.position());
            if(result!=DomainStatus::Applied) return result;
        }
    }
    if(doFunction==11 || doFunction==14) {
        const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
        if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto target=participants.find(pending.unit);
        if(target.id()) {
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
            const auto calculation=[&](std::string_view field) {
                const auto result=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
                if(!result) throw std::logic_error("Missing native spear follow-up formula");
                return *result;
            };
            const auto &row=caster->programs()->formulas->skills.at(pending.skill.sourceId);
            const auto missile=ports_.missiles.nativeDefinition(row.reference("srvmissilea"));
            if(!missile) return DomainStatus::Unavailable;
            std::vector<missiles::NativeSpawn> births;
            if(doFunction==11) {
                const int count=calculation("calc1");
                for(int index=0;index<count;++index) {
                    missiles::NativeSpawn spawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,target.position(),
                        target.position()*2.f-caster->position(),*missile,pending.skill.sourceId,pending.skill.rank};
                    spawn.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;spawn.initializationIndex=index;
                    births.push_back(std::move(spawn));
                }
            } else {
                const auto candidates=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,calculation("calc1"),0xa783,false,
                    target.position()+ports_.areas.at(actor.area).definition.origin,effects::NativeAuraTargetMode::TargetRange,actor.tick});
                const auto next=nativeSkillSuccessor(candidates,uint32_t(target.id().value));
                if(next) {
                    // Retail 1.13c Do14 (6FC6CFE4/6FC6CFF1) calls X then Y;
                    // D2MOO's Y/Y expression is a transcription difference.
                    missiles::NativeSpawn spawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,target.position(),
                        participants.find(next).position(),*missile,pending.skill.sourceId,pending.skill.rank};
                    spawn.finalParameters=std::array<int32_t,2>{calculation("calc2"),0};births.push_back(std::move(spawn));
                }
            }
            if(!births.empty()) {
                auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return prepared.status;
                const auto born=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!born) return born.status;
            }
        }
    }
    if(doFunction==83) {
        // Fire Hit drains and frees this combat, then calls its start
        // function again, even after the sequence's final hit event.
        if(!execution.following) {
            auto following=pending;following.actor=actor;
            following.weaponMelee=execution.following=std::make_shared<WeaponMeleeExecution>();
            runtime_->weaponStarts.insert_or_assign(actor.actor,std::move(following));
        }
        auto following=pending;following.weaponMelee=execution.following;
        const auto program=execution.contact->monsterProgram;
        const auto prepared=startWeaponMelee(following,actor,program?&*program:nullptr);
        if(!prepared) return prepared.status;
        execution.occurrence.reset();pending.weaponMelee=std::move(following.weaponMelee);
        pending.nativeCallbackStarted=false;return DomainStatus::Applied;
    }
    pending.nativeCallbackStarted=false;execution.occurrence.reset();return DomainStatus::Applied;
}
}
