#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
#include "native_caster.hpp"
#include "assassin_kick.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/corpse_explosion.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainResult<std::shared_ptr<actions::SkillRelease>> System::startShadowSkill(const CastRequest &request,const MonsterAttackRule &attack) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,request.actor);
    if(!caster || !caster->monster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    const auto unit=std::get<UnitTarget>(request.target);
    const auto position=ports_.monsters.targetPosition(unit.id,caster->area());
    const auto target=request.position?*request.position:position?position->first:caster->position();
    auto actor=caster->context(ports_.areas,request.tick);actor.sequence=request.tick+1;
    auto prior=runtime_->assassinStarts.find(caster->id());
    if(prior!=runtime_->assassinStarts.end() && (prior->second.skill.sourceId!=request.skill || prior->second.unit!=unit.id)) {
        if(prior->second.assassinKick && prior->second.assassinKick->occurrence) ports_.combat.discardNativeMelee(caster->id(),*prior->second.assassinKick->occurrence);
        runtime_->assassinStarts.erase(prior);prior=runtime_->assassinStarts.end();
    }
    if(prior==runtime_->assassinStarts.end()) {
        auto skill=evaluateNativeSkill(*caster,ports_.effects,request.skill,attack.rank,request.tick);
        Release release{actor,std::move(skill),{}, {actor.area,actor.areaGeneration,target},unit.id,request.tick,unit.type};
        prior=runtime_->assassinStarts.emplace(caster->id(),std::move(release)).first;
    }
    auto &release=prior->second;
    actor.sequence=release.actor.sequence;
    release.actor.tick=request.tick;
    if(!release.nativeStartPrepared) {
        if(release.skill.weapon) {
            if(!caster->monster->equipmentStats) return {DomainStatus::Unavailable,{}};
            const auto &gear=*caster->monster->equipmentStats;
            const auto &main=gear.weapons[0];const auto &program=*release.skill.weapon;
            if(!program.requiredType.empty() && program.requiredType!="mele" &&
               std::find(main.types.begin(),main.types.end(),program.requiredType)==main.types.end()) return {DomainStatus::Unavailable,{}};
            const bool dual=gear.weaponCount>1 && gear.weapons[1].item;
            if(program.dualWield && !dual) return {DomainStatus::Unavailable,{}};
            release.weapon=main;
            if(dual) release.offhand=gear.weapons[1];
            if(release.skill.assassin && (release.skill.assassin->program==AssassinProgram::DragonTalon ||
               release.skill.assassin->program==AssassinProgram::DragonTail || release.skill.assassin->program==AssassinProgram::DragonFlight)) {
                // Kicks use their own held-combat cursor; the weapon is a
                // sampled stat source, not the action dispatch discriminator.
                release.weapon.reset();release.offhand.reset();
                const auto result=startAssassinKick(release,actor);if(!result) return {result.status,{}};
            } else if(release.skill.assassin && release.skill.assassin->startFunction==23) {
                const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto victim=participants.find(unit.id);
                if(!victim.aliveIn(actor.area) || !unitMeleeReach(ports_.areas.at(actor.area).definition.collision,caster->position(),caster->monster->rule.size,victim.position(),victim.size(),caster->monster->rule.meleeRange))
                    return {DomainStatus::Unavailable,{}};
            }
        }
        if(release.skill.assassin) {
            const auto role=release.skill.assassin->program;
            if(role==AssassinProgram::BladeFury) {const auto started=startBladeFury(release,actor);if(!started) return {started.status,{}};}
            if(role==AssassinProgram::BladeShield) {const auto started=ports_.effects.unitSkill(caster->id(),release.skill,request.tick);if(!started) return {started.status,{}};release.manaPaid=true;}
        }
        release.nativeStartPrepared=true;
    }
    return {DomainStatus::Applied,std::make_shared<Release>(release)};
}
DomainResult<bool> System::releaseShadowSkill(MonsterRelease &pending,TickContext tick) {
    if(!pending.nativeSkill) return {DomainStatus::Unavailable,{}};
    auto &release=*pending.nativeSkill;const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    auto actor=release.actor;actor.tick=tick.tick;
    const auto unit=std::get<UnitTarget>(pending.request.target);
    const auto position=ports_.monsters.targetPosition(unit.id,pending.area);
    const auto target=pending.request.position?*pending.request.position:position?position->first:release.target.position;
    DomainStatus status;
    if(release.weapon) {
        if(!release.nativeMeleeOccurrence) {
            const auto &weapon=release.offhand && pending.nextRelease%2?*release.offhand:*release.weapon;
            WeaponSkillDamage damage;damage.weapon=weapon;damage.level=caster->level();
            missiles::Spawn request{actor,release.skill,{},target,true};request.weapon=damage;
            const auto contact=ports_.missiles.directContact(request,{unit.id});if(!contact) return {contact.status,{}};
            release.nativeMeleeOccurrence=contact.value->occurrence;release.nativeCallbackStarted=true;
        }
        status=ports_.combat.advanceAuraPulse(caster->id(),*release.nativeMeleeOccurrence,{tick.tick})==StepStatus::Blocked?DomainStatus::Capacity:DomainStatus::Applied;
        if(status==DomainStatus::Applied) {release.nativeMeleeOccurrence.reset();release.nativeCallbackStarted=false;}
    } else if(release.skill.assassin && release.skill.assassin->program==AssassinProgram::Defensive) status=ports_.effects.unitSkill(caster->id(),release.skill,tick.tick).status;
    else status=activateAssassin(release,actor,target);
    pending.nativeCallbackStarted=release.nativeCallbackStarted;
    if(status!=DomainStatus::Applied) return {status,{}};
    if(release.assassinKick && release.assassinKick->phase!=AssassinKickExecution::Phase::Complete) {
        if(release.assassinKick->phase==AssassinKickExecution::Phase::FlightAllocation && pending.releaseFrames.size()>1) {
            pending.nextRelease=1;release.weaponHits=pending.releaseFrames;
            pending.due=pending.started+uint64_t(pending.releaseFrames[1]);
        } else pending.due=release.tick;
        const auto extended=ports_.monsters.extendAttack(caster->id(),pending.due+uint64_t(std::max(1,release.weaponFrames)));
        if(!extended) return {extended.status,{}};
        return {DomainStatus::Applied,false};
    }
    if(release.skill.assassin && release.skill.assassin->program==AssassinProgram::BladeFury &&
       ports_.effects.unitStates(caster->id(),tick.tick).contains(caster->programs()->formulas->infernoState)) {
        pending.due=tick.tick+1;ports_.monsters.extendAttack(caster->id(),tick.tick+7);return {DomainStatus::Applied,false};
    }
    if(release.weapon && ++pending.nextRelease<pending.releaseFrames.size()) {
        pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);return {DomainStatus::Applied,false};
    }
    return {DomainStatus::Applied,true};
}
DomainResult<bool> System::releaseAssassinPet(MonsterRelease &pending,TickContext tick) {
    const auto done=[] {return DomainResult<bool>{DomainStatus::Applied,true};};
    const auto *body=ports_.monsters.find(pending.request.actor);if(!body || !body->assassinPet) return {DomainStatus::InvalidActor,{}};
    const auto &catalog=*body->rule.formulas;const int skill=pending.request.skill,rank=pending.attack.rank;
    const auto &record=catalog.skills.at(skill);const int function=record.number("srvdofunc");
    if(body->assassinPet->definition->warp) return releaseShadowSkill(pending,tick);
    const auto unit=std::get<UnitTarget>(pending.request.target);
    if(function==55) {
        const auto result=ports_.effects.corpseExplosion(body->id,unit.id,skill,rank,tick.tick,pending.corpseExplosion);
        pending.nativeCallbackStarted=pending.corpseExplosion && pending.corpseExplosion->prepared && !pending.corpseExplosion->complete;
        return result?done():DomainResult<bool>{result.status,{}};
    }
    const auto target=ports_.monsters.targetPosition(unit.id,body->area);
    if(!target) {
        if(function==95) ports_.monsters.beginAttack(body->id,tick.tick+uint64_t(body->assassinPet->definition->infernoFrames),true,{});
        return done();
    }
    const auto sampled=ports_.effects.unitStatValues(body->id,tick.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
    auto input=formulaInput(*body,*sampled);SkillFormulaValues values(catalog,input);
    const auto calc=[&](std::string_view field) {const auto result=values.calculation(FormulaDomain::Skill,skill,rank,field);if(!result) throw std::logic_error("Missing native assassin pet skill calculation");return *result;};
    if(function==0 || function==17 || function==125 || function==95) {
        const auto name=record.reference(function==0?"srvmissile":"srvmissilea");
        const auto missile=ports_.missiles.nativeDefinition(name);if(!missile) return {DomainStatus::Unavailable,{}};
        const int count=function==17?calc("calc1"):1;
        if(count>512) return {DomainStatus::Capacity,{}};
        std::vector<missiles::NativeSpawn> births;
        for(int index=0;index<count;++index) {
            missiles::NativeSpawn spawn{body->id,body->area,pending.generation,tick.tick,body->position,target->first,*missile,skill,rank};
            spawn.copyMonsterAttackBonus=function==0;
            if(function==17) {spawn.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;spawn.initializationIndex=index;}
            if(function==125) {
                const int x=int(target->first.x)-int(body->position.x),y=int(target->first.y)-int(body->position.y);
                spawn.finalParameters=std::array<int32_t,2>{-y,x};
            }
            if(function==95) {
                // Retail 1.13c SrvDo95 leaves D2MissileStrc.nSkill zero;
                // damage uses the missile's linked Skills record.
                spawn.skill=0;
                const auto &data=catalog.missiles.at(*missile);int frames=calc("calc1");if(frames<=0) frames=nativeAdd(data.number("param2"),rank-1);
                spawn.totalFrames=std::clamp(frames,1,255);
                spawn.nativeVelocity=nativeAdd(data.number("vel"),nativeMultiply(rank,data.number("vellev"))/8);
            }
            births.push_back(std::move(spawn));
        }
        if(!births.empty()) {
            auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
            const auto committed=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!committed) return {committed.status,{}};
        }
        if(function==95) {
            const bool more=tick.tick<pending.infernoUntil && ports_.effects.unitStates(body->id,tick.tick).contains(catalog.infernoState);
            const auto delay=uint64_t(more?std::max(1,calc("calc3")):body->assassinPet->definition->infernoFrames);
            ports_.monsters.beginAttack(body->id,tick.tick+delay,true,unit.id);
            if(more) {pending.due=tick.tick+delay;return {DomainStatus::Applied,false};}
        }
        return done();
    }
    return releaseShadowSkill(pending,tick);
}
}
