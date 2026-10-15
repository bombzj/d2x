#include "system.hpp"
#include "runtime.hpp"
#include "native_caster.hpp"
#include "druid_melee.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <stdexcept>
namespace d2x::server::skills {
DomainResult<> System::startDruidPet(MonsterRelease &pending,TickContext tick) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->monster->druidPet || !caster->programs() || !caster->monster->alive()) return {DomainStatus::InvalidActor,{}};
    const auto &rules=*caster->programs();const auto &record=rules.formulas->skills.at(pending.request.skill);
    const auto actor=caster->context(ports_.areas,tick.tick);const auto unit=std::get<UnitTarget>(pending.request.target);
    if(!pending.nativeSkill) {
        SkillCastSpec skill;skill.sourceId=pending.request.skill;skill.rank=pending.attack.rank;skill.effect=SkillBehavior::Druid;
        Release release{actor,std::move(skill),{}, {actor.area,actor.areaGeneration,pending.launch?pending.launch->second:caster->position()},unit.id,tick.tick,unit.type};
        if(record.number("srvstfunc")==32) {
            auto spec=std::make_shared<DruidSkillSpec>();spec->program=DruidProgram::BearSmite;spec->startFunction=32;spec->doFunction=2;
            release.skill.druid=std::move(spec);release.skill.effect=SkillBehavior::WeaponProjectile;release.skill.weapon.emplace();release.skill.weapon->mode="a1";
            auto input=caster->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*rules.formulas,input);
            const auto damage=values.calculation(FormulaDomain::Skill,release.skill.sourceId,release.skill.rank,"calc1");
            const auto toHit=values.parameter(FormulaDomain::Skill,release.skill.sourceId,release.skill.rank,"toht");
            if(!damage || !toHit) throw std::logic_error("Missing native BearSmite start formula");
            release.skill.weapon->damagePercent=*damage;release.skill.weapon->attackRating=*toHit;
            release.weapon=caster->monster->petWeapon.value_or(WeaponDamage{});
        }
        pending.nativeSkill=std::make_shared<Release>(std::move(release));
    }
    auto &release=*pending.nativeSkill;if(release.nativeStartPrepared) return {DomainStatus::Applied,std::monostate{}};
    if(record.number("srvstfunc")==32) {
        const auto started=startDruidMelee(release,actor);if(!started) return started;
        auto input=caster->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*rules.formulas,input);
        const auto addition=values.calculation(FormulaDomain::Skill,release.skill.sourceId,release.skill.rank,"calc2");
        if(!addition) throw std::logic_error("Missing native BearSmite allocated damage formula");
        ports_.combat.addAllocatedPhysical(actor.actor,*release.druidMelee->occurrence,nativeShiftLeft(*addition,8));
        runtime_->druidStarts.erase(actor.actor);
    }
    if(record.number("srvstfunc")==63) {
        const auto *owner=caster->controller;const auto *corpse=ports_.monsters.find(unit.id);
        if(!owner || !corpse || ports_.effects.unitStates(unit.id,tick.tick).contains(rules.formulas->corpseNoSelectState)) return {DomainStatus::Unavailable,{}};
        const auto definition=ports_.missiles.nativeDefinition(record.reference("srvmissilea"));if(!definition) return {DomainStatus::Unavailable,{}};
        auto consumed=ports_.effects.prepareConsumedCorpse(unit.id,rules.formulas->corpseNoSelectState,tick.tick);if(!consumed) return {consumed.status,{}};
        missiles::NativeSpawn spawn{owner->actor,corpse->area,ports_.areas.at(corpse->area).generation,tick.tick,corpse->position,corpse->position,*definition,pending.request.skill,pending.attack.rank};
        const std::array<DomainFact,1> extra{SkillPulseFact{caster->id(),unit.id,1,1,corpse->area,pending.request.skill,1,corpse->position}};
        auto birth=ports_.missiles.prepareNativeBirth(std::span{&spawn,size_t{1}},0,extra);if(!birth) return {birth.status,{}};
        const ActorContext context{owner->player,owner->actor,owner->area,ports_.areas.at(owner->area).generation,0,tick.tick};
        transactions::CharacterEdit edit{context,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
        auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},*consumed.value);if(!plan) return {plan.status,{}};
        plan.value->missileBirth=std::move(*birth.value);const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    }
    release.nativeStartPrepared=true;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<bool> System::releaseDruidPet(MonsterRelease &pending,TickContext tick) {
    const auto done=[] {return DomainResult<bool>{DomainStatus::Applied,true};};
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->programs() || !pending.nativeSkill) return {DomainStatus::InvalidActor,{}};
    const auto actor=caster->context(ports_.areas,tick.tick);auto &release=*pending.nativeSkill;
    const auto &rules=*caster->programs();const auto &record=rules.formulas->skills.at(pending.request.skill);
    const auto unit=std::get<UnitTarget>(pending.request.target);const int function=record.number("srvdofunc");
    if(release.druidMelee) {
        pending.nativeCallbackStarted=true;
        const auto applied=activateDruidMelee(release,actor,release.target.position);
        pending.nativeCallbackStarted=applied==DomainStatus::Capacity;
        return applied==DomainStatus::Applied?done():DomainResult<bool>{applied,{}};
    }
    if(record.number("srvstfunc")==63) return done();
    if(function==98) {
        const auto *owner=caster->controller;if(!owner) return {DomainStatus::InvalidActor,{}};
        const auto &area=ports_.areas.at(caster->area());
        const auto destination=pending.launch?pending.launch->second:caster->position();const auto global=destination+area.definition.origin;
        const auto room=ports_.areas.roomAtPosition(caster->area(),caster->position(),int(global.x),int(global.y));if(!room) return done();
        const auto point=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),caster->monster->rule.size,0x1c09,ports_.spatial.nativeCollisionSample(0x1c09));if(!point) return done();
        const ActorContext context{owner->player,owner->actor,point->room.area,ports_.areas.at(point->room.area).generation,0,tick.tick};
        const auto warped=ports_.monsters.warpPet(caster->id(),context,point->position);return warped?done():DomainResult<bool>{warped.status,{}};
    }
    auto input=caster->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto calc=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,field);
        if(!value) throw std::logic_error("Unavailable native druid pet formula");return *value;};
    if(function==137) {
        if(!release.manaPaid) {
            const auto consumed=ports_.effects.prepareConsumedCorpse(unit.id,rules.formulas->corpseNoDrawState,tick.tick);if(!consumed) return {consumed.status,{}};
            const auto *corpse=ports_.monsters.find(unit.id);if(!corpse) return {DomainStatus::Unavailable,{}};
            const ActorContext recipient{actor.player,corpse->id,corpse->area,ports_.areas.at(corpse->area).generation,actor.sequence,tick.tick};
            const auto committed=ports_.transactions.commitUnitAuras({},*consumed.value,recipient);if(!committed) return {committed.status,{}};
            release.manaPaid=true;
        }
        SkillCastSpec skill=release.skill;CombatEffectSpec effect;effect.state=caster->monster->rule.states->at(record.auraState);
        effect.source={CombatEffectSource::Skill,caster->id(),pending.request.skill,pending.attack.rank};effect.nativeState=effect.nativeDirectAllocation=effect.refreshPassivesOnRemoval=true;
        effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;effect.duration=uint32_t(calc("auralencalc"));effect.nativeValues.emplace();effect.stacking=EffectStacking::ReuseState;
        skill.appliedEffect=std::move(effect);skill.nativeEffectWrites=values.auraStats(pending.request.skill,pending.attack.rank,6,true);
        const auto installed=ports_.effects.unitSkill(caster->id(),skill,tick.tick);return installed?done():DomainResult<bool>{installed.status,{}};
    }
    if(function==130) {
        const auto target=ports_.monsters.targetPosition(unit.id,caster->area());if(!target) return done();
        const auto missile=ports_.missiles.nativeDefinition(record.reference("srvmissilea"));if(!missile) return {DomainStatus::Unavailable,{}};
        const int count=calc("calc1");if(count<=0) return done();if(count>512) return {DomainStatus::Capacity,{}};
        constexpr std::array<Vec,4> offsets{{{0,1},{0,-1},{1,0},{-1,0}}};std::vector<missiles::NativeSpawn> births;
        for(int index=0;index<count;++index) {
            missiles::NativeSpawn spawn{caster->id(),caster->area(),actor.areaGeneration,tick.tick,target->first,target->first+offsets[size_t(index)%4],*missile,pending.request.skill,pending.attack.rank};
            spawn.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;spawn.initializationIndex=index;births.push_back(spawn);
        }
        auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
        const auto born=ports_.missiles.commitNativeBirth(std::move(*prepared.value));return born?done():DomainResult<bool>{born.status,{}};
    }
    if(!pending.request.skill) {
        if(!release.nativeMeleeOccurrence) {
            release.skill=rules.definitions.at(0).spec.sourceId==0?evaluateNativeSkill(*caster,ports_.effects,0,1,tick.tick):SkillCastSpec{};
            release.skill.weapon.emplace();release.skill.weapon->commonAttack=true;
            const auto target=ports_.monsters.targetPosition(unit.id,caster->area());if(!target) return done();
            const auto occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);
            missiles::Spawn request{actor,release.skill,{},target->first,true};request.nativeMeleeOccurrence=occurrence;
            WeaponSkillDamage weapon;weapon.weapon=caster->monster->petWeapon.value_or(WeaponDamage{});weapon.level=caster->level();request.weapon=std::move(weapon);
            const auto queued=ports_.missiles.directContact(request,{unit.id});if(!queued) {--runtime_->nativeKickOccurrence;return {queued.status,{}};}
            release.nativeMeleeOccurrence=occurrence;
        }
        pending.nativeCallbackStarted=true;
        if(ports_.combat.advanceAuraPulse(caster->id(),*release.nativeMeleeOccurrence,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        release.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;return done();
    }
    return {DomainStatus::NotImplemented,{}};
}
}
