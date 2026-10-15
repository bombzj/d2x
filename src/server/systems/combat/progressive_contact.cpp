#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::combat {
struct ProgressiveContactExecution {
    enum class Phase { Select,Write,Program,Notify,Remove,Complete } phase{};
    int lastState{-1};AssassinCharge charge;
    std::vector<AssassinProgressiveStep> steps;size_t next{};
    uint32_t clientSeed{};bool mainDrained{},areaQueued{};
    std::vector<uint64_t> areaOccurrences;size_t areaNext{};
};
DomainResult<bool> System::advanceProgressive(SpellImpact &impact,TickContext tick,bool drained) {
    if(!impact.nativeSkill || !impact.nativeSkill->progressive) return {DomainStatus::Applied,true};
    if(!impact.nativeSkill->release) impact.nativeSkill->release=std::make_shared<ProgressiveContactExecution>();
    auto &execution=*impact.nativeSkill->release;execution.mainDrained|=drained;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,impact.source);
    if(!caster || !caster->programs()) return {DomainStatus::Stale,{}};
    const auto &rules=*caster->programs();const auto &area=ports_.areas.at(caster->area());
    const auto actor=caster->context(ports_.areas,tick.tick);
    const auto consume=[&](uint64_t expected,uint64_t next) {
        return caster->monster?ports_.monsters.consumeRandom(caster->id(),expected,next):ports_.transactions.consumeRandom(actor,expected,next);
    };
    using Phase=ProgressiveContactExecution::Phase;
    for(;;) {
        const Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto target=participants.find(impact.targets.at(impact.next));
        if(!target.id()) return {DomainStatus::Stale,{}};
        if(execution.phase==Phase::Select) {
            const auto charges=ports_.effects.progressiveCharges(caster->id());
            const auto next=std::find_if(charges.begin(),charges.end(),[&](const auto &entry){return entry.state>execution.lastState;});
            if(next==charges.end()) {execution.phase=Phase::Complete;continue;}
            if(!unitMeleeReach(area.definition.collision,caster->position(),caster->player?2:caster->monster->rule.size,target.position(),target.size(),
                caster->player && impact.weapon?impact.weapon->weapon.rangeAdder:caster->monster?caster->monster->rule.meleeRange:0)) return {DomainStatus::Applied,true};
            execution.charge=*next;execution.charge.count=std::clamp(execution.charge.count,1,3);
            const auto &program=*execution.charge.program;
            execution.steps=assassinProgressiveSteps(execution.charge.count,program.progressiveStack,program.progressiveFunctions);
            execution.next=0;auto seed=caster->random();execution.clientSeed=rollRandom(seed);
            execution.phase=Phase::Write;
        }
        if(execution.phase==Phase::Write) {
            const auto &step=execution.steps.at(execution.next);
            if(execution.charge.program->progressiveStack) {
                const auto result=ports_.effects.progressiveValue(actor,execution.charge,step.charge);
                if(!result) return {result.status,{}};
            }
            execution.phase=Phase::Program;
        }
        if(execution.phase==Phase::Program) {
            const auto &charge=execution.charge;const auto &program=*charge.program;
            const auto &step=execution.steps.at(execution.next);
            auto input=caster->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*rules.formulas,input);
            const auto formula=[&](std::string_view field) {
                const auto value=values.calculation(FormulaDomain::Skill,charge.skill,charge.rank,field);
                if(!value) throw std::logic_error("Unavailable native progressive release formula");return *value;
            };
            if(step.function==38) {
                // SrvDo038 drains the already allocated main contact before
                // its area callback. Return to its owner without recursing
                // into the same queued contact or repeating target HP.
                if(!execution.mainDrained) return {DomainStatus::Applied,false};
                if(!execution.areaQueued) {
                    const auto expected=caster->random();auto random=expected;
                    const auto damage=rollNativeSkillDamage(values,charge.skill,charge.rank,random);
                    int radius=formula("prgcalc"+std::to_string(step.charge));if(!radius) radius=formula("aurarangecalc");
                    const auto &native=rules.formulas->skills.at(charge.skill);
                    auto targets=ports_.effects.nativeAuraCandidateRecipients({caster->id(),actor.area,radius,uint32_t(native.number("aurafilter")),false,
                        target.position()+ports_.areas.at(target.player?target.player->area:target.monster->area).definition.origin, effects::NativeAuraTargetMode::TargetRange,tick.tick});
                    if(targets.size()>((uint64_t{1}<<56)-1)-state_.progressiveOccurrence) return {DomainStatus::Capacity,{}};
                    SpellImpact pulse;pulse.projectile=pulse.source=caster->id();pulse.area=actor.area;pulse.nativeAura=true;pulse.synchronous=true;pulse.retainDeadSource=true;
                    pulse.occurrence=(uint64_t{1}<<60)|(state_.progressiveOccurrence+1);pulse.targets=std::move(targets);pulse.nativeDamageFlagsOnly=true;pulse.unblockable=true;
                    pulse.areaDamage=MissileAreaDamage{};pulse.areaDamage->resultFlags=1;pulse.areaDamage->hitFlags=1;
                    pulse.areaDamage->rolled=std::make_shared<MissileAreaRoll>();pulse.areaDamage->rolled->value=damage;
                    pulse.missileDamage=std::make_shared<MissileDamageRollState>();pulse.missileDamage->formulas=rules.formulas;pulse.missileDamage->statRules=rules.stats;
                    pulse.missileDamage->uninterruptableState=rules.uninterruptable.id;
                    pulse.monsterStates={rules.chilled,rules.poisoned,{}};
                    std::vector<SpellImpact> pulses;pulses.reserve(targets.size());
                    // Each callback owns its destination area while sharing
                    // the one source roll and native finder order.
                    for(size_t index=0;index<pulse.targets.size();++index) {
                        const auto recipient=participants.find(pulse.targets[index]);auto child=pulse;
                        child.area=recipient.player?recipient.player->area:recipient.monster->area;
                        child.targets={pulse.targets[index]};child.occurrence=(uint64_t{1}<<60)|(state_.progressiveOccurrence+index+1);
                        pulses.push_back(std::move(child));
                    }
                    std::vector<uint64_t> occurrences;for(const auto &child:pulses) occurrences.push_back(child.occurrence);
                    auto queued=prepareSpells(std::move(pulses));if(!queued) return {queued.status,{}};
                    const auto consumed=consume(expected,random);if(!consumed) return {consumed.status,{}};
                    commitSpells(std::move(*queued.value));state_.progressiveOccurrence+=occurrences.size();
                    execution.areaOccurrences=std::move(occurrences);execution.areaNext=0;execution.areaQueued=true;
                }
                while(execution.areaNext<execution.areaOccurrences.size()) {
                    if(advanceAuraPulse(caster->id(),execution.areaOccurrences[execution.areaNext],tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
                    ++execution.areaNext;
                }
                execution.areaQueued=false;
            } else if(step.function) {
                const auto missile=program.missiles.at(size_t(step.charge-1));
                if(missile>0 && charge.rank>0) {
                    const auto expected=caster->random();auto ownerRandom=expected;
                    const auto make=[&](Vec position,Vec destination) {
                        return missiles::NativeSpawn{caster->id(),actor.area,actor.areaGeneration,tick.tick,position,destination,missile,charge.skill,charge.rank};
                    };
                    std::vector<missiles::NativeSpawn> births;
                    const auto at=target.position();
                    if(step.function==40) births.push_back(make(at,at));
                    else if(step.function==36) {
                        const auto &definition=rules.formulas->missiles.at(missile);
                        const auto velocity=nativeAdd(formula("calc1"),nativeAdd(definition.number("vel"),nativeMultiply(charge.rank,definition.number("vellev"))/8));
                        for(int index=0;index<64;++index) {auto spawn=make(at,at+missileRingDirection(index));if(velocity) spawn.nativeVelocity=velocity;births.push_back(spawn);}
                    } else if(step.function==39) {
                        auto random=initialRandom(rollRandom(ownerRandom));int radius=formula("prgcalc"+std::to_string(step.charge));if(!radius) radius=formula("aurarangecalc");
                        const auto size=nativeMultiply(radius,radius);
                        for(int index=0;index<size;++index) {
                            const auto x=nativeSubtract(radius,int32_t(limitedRandom(random,uint32_t(nativeMultiply(2,radius)))));
                            const auto y=nativeSubtract(radius,int32_t(limitedRandom(random,uint32_t(nativeMultiply(2,radius)))));
                            if(nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y))<=size) births.push_back(make(at+Vec{float(x),float(y)},at+Vec{float(x),float(y)}));
                        }
                    } else if(step.function==37 || step.function==143) {
                        int stride=formula("prgcalc"+std::to_string(step.charge));if(!stride) stride=formula("aurarangecalc");
                        if(stride<=0) return {DomainStatus::InvalidRequest,{}};
                        auto random=initialRandom(rollRandom(ownerRandom));
                        for(int index=0;index<64;index+=stride) {
                            auto spawn=make(at,at+missileRingDirection(index));spawn.initialSeed=rollRandom(random);
                            spawn.initialization=step.function==37?missiles::NativeSpawn::Initialization::ProgressiveCharged:missiles::NativeSpawn::Initialization::ProgressiveSpiral;
                            if(step.function==37 || program.program==AssassinProgram::Charge) {
                                const auto phoenix=rules.formulas->skillIds.find("royal strike");
                                const auto parameter=phoenix==rules.formulas->skillIds.end()?0:rules.formulas->skills.at(phoenix->second).number("param2");
                                spawn.finalParameters=std::array<int32_t,2>{nativeAdd(parameter,1),0};
                            }
                            births.push_back(spawn);
                        }
                    } else if(step.function==41) {
                        auto random=initialRandom(rollRandom(ownerRandom));const auto count=formula("prgcalc"+std::to_string(step.charge));
                        for(int index=0;index<count;++index) {
                            int x=int(rollRandom(random)%40)-20,y=int(rollRandom(random)%40)-20;if(!x && !y) x=20;
                            auto spawn=make(at,at+Vec{float(x),float(y)});
                            spawn.finalParameters=std::array<int32_t,2>{nativeNarrow(uint32_t(random)),nativeNarrow(uint32_t(uint16_t(x))|(uint32_t(uint16_t(y))<<16))};births.push_back(spawn);
                        }
                    } else return {DomainStatus::NotImplemented,{}};
                    if(!births.empty()) {
                        if(ownerRandom!=expected) births.front().ownerSeedEdit=std::pair{expected,ownerRandom};
                        auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
                        const auto committed=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!committed) return {committed.status,{}};
                    } else if(ownerRandom!=expected) {
                        const auto result=consume(expected,ownerRandom);if(!result) return {result.status,{}};
                    }
                }
            }
            if(++execution.next<execution.steps.size()) execution.phase=Phase::Write;
            else execution.phase=Phase::Notify;
        }
        if(execution.phase==Phase::Notify) {
            const auto &charge=execution.charge;
            const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},
                {SkillPulseFact{caster->id(),target.id(),uint8_t(caster->monster?1:0),uint8_t(target.player?0:1),actor.area,charge.skill,charge.rank,target.position(),uint8_t(charge.count),execution.clientSeed,0}}});
            if(!result) return {result.status,{}};execution.phase=Phase::Remove;
        }
        if(execution.phase==Phase::Remove) {
            const auto result=ports_.effects.removeState(actor,execution.charge.state);if(!result) return {result.status,{}};
            execution.lastState=execution.charge.state;execution.phase=Phase::Select;
        }
        if(execution.phase==Phase::Complete) return {DomainStatus::Applied,true};
    }
}
}
