#include "system.hpp"
#include "runtime.hpp"
#include "native_caster.hpp"
#include "evaluation.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::skills {
DomainResult<> System::startMonsterWhirlwind(const CastRequest &request,const MonsterAttackRule &program) {
    const auto *source=ports_.monsters.find(request.actor);if(!source || !request.position) return {DomainStatus::InvalidActor,{}};
    if(source->nativeActionRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto formulas=source->rule.formulas;const auto &row=formulas->skills.at(request.skill);
    if(ports_.effects.unitStates(source->id,request.tick).contains(formulas->infernoState)) return {DomainStatus::Unavailable,{}};
    auto [entry,fresh]=runtime_->monsterWhirlwindStarts.try_emplace(request.actor);
    auto &start=entry->second;
    if(fresh || start.tick!=request.tick || start.skill!=request.skill) start={request.tick,request.skill,*request.position};
    if(!start.stage) {
        const auto global=start.destination+ports_.areas.at(source->area).definition.origin;
        if(!int(std::floor(global.x)) || !int(std::floor(global.y))) return {DomainStatus::Unavailable,{}};
        const auto moved=ports_.monsters.startWhirlwindMotion(source->id,start.destination);if(!moved) return moved;
        ++start.stage;
    }
    while(start.stage<=3) {
        source=ports_.monsters.find(request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
        const int state=start.stage==1?source->rule.uninterruptableState:start.stage==2?source->rule.skillMoveState:row.auraState;
        if(state<0 || !source->rule.states->contains(state)) return {DomainStatus::Unavailable,{}};
        SkillCastSpec skill;skill.sourceId=request.skill;skill.rank=program.rank;
        CombatEffectSpec effect;effect.state=source->rule.states->at(state);effect.nativeState=true;effect.nativeDirectAllocation=true;
        effect.nativeValues.emplace();effect.modeActionRevision=source->nativeActionRevision+1;
        effect.stacking=EffectStacking::SkillLevel;effect.refreshPassivesOnRemoval=true;
        const auto expected=source->combatRandom;auto random=expected;
        if(start.stage==3) {
            const auto stats=ports_.effects.unitStatValues(source->id,request.tick);if(!stats) return {DomainStatus::Unavailable,{}};
            const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
                return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
            };
            auto input=formulaInput(*source,*stats,draw);SkillFormulaValues values(*formulas,input);
            const auto duration=values.calculation(FormulaDomain::Skill,request.skill,program.rank,"auralencalc");if(!duration) return {DomainStatus::Unavailable,{}};
            if(*duration) effect.duration=uint32_t(*duration);
            skill.nativeEffectWrites=values.auraStats(request.skill,program.rank,6,true);
            const auto passive=values.passiveBuffStats(request.skill,program.rank);
            skill.nativeEffectWrites.insert(skill.nativeEffectWrites.end(),passive.begin(),passive.end());
            const auto original=skill.nativeEffectWrites;
            for(const auto &stat:original) if(stat.key.id==source->rule.stats->names.at("attackrate"))
                skill.nativeEffectWrites.emplace_back(StatKey{source->rule.stats->names.at("other_animrate"),0},stat.value);
            skill.nativeEffectWrites.emplace_back(StatKey{source->rule.stats->names.at("modifierlist_skill"),0},request.skill);
            skill.nativeEffectWrites.emplace_back(StatKey{source->rule.stats->names.at("modifierlist_level"),0},program.rank);
            effect.registersUnitEvents=true;
            for(const auto binding:values.unitEvents(request.skill)) {if(binding.event<0) break;if(binding.function>0) effect.unitEvents.push_back(binding);}
        }
        skill.appliedEffect=std::move(effect);
        const auto applied=ports_.effects.unitSkill(request.actor,skill,request.tick,std::pair{expected,random});if(!applied) return applied;
        ++start.stage;
    }
    return {DomainStatus::Applied,std::monostate{}};
}

DomainResult<> System::monsterWhirlwindEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto id=event.owner.unit;auto clock=runtime_->monsterModes.find(id);auto release=runtime_->monsters.find(id);
    if(clock==runtime_->monsterModes.end() || release==runtime_->monsters.end()) {cancel({},id);return done();}
    auto &mode=clock->second;auto &pending=release->second;
    if(mode.advanced && mode.lastFrame==tick) return done();
    if(mode.animation.remaining<=0) {
        pending.nativeCallbackStarted=true;
        {auto guard=state_.executingRelease(id);
            const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
            const auto died=ports_.monsters.settleDelayedDeath(id,tick);if(!died) return died;}
        ports_.monsters.stopScriptedMotion(id);runtime_->monsters.erase(release);state_.interrupt(id,tick);
        ports_.monsters.finishAction(id,tick);ports_.frameEvents.cancel(event.handle);runtime_->monsterModes.erase(clock);ports_.monsters.resumeMode(id,tick);return done();
    }
    const auto *source=ports_.monsters.find(id);if(!source) {cancel({},id);return done();}
    if(!mode.movementAdvanced || mode.movementFrame!=tick) {
        if(!ports_.monsters.advanceScriptedMotion(id,tick)) mode.whirlwindStopped=true;
        mode.movementFrame=tick;mode.movementAdvanced=true;
    }
    if(pending.nativeCallbackStarted || mode.step.event==1 || mode.step.event==2 || mode.step.event==4) {
        if(!mode.whirlwindRolled && !mode.whirlwindContact && !pending.nativeCallbackStarted) pending.pd2CastEventPosted=false;
        postPd2MonsterCastItemEvent(pending,tick);
        if(mode.whirlwindStopped) {
            pending.nativeCallbackStarted=true;
            {auto guard=state_.executingRelease(id);
                const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
                const auto died=ports_.monsters.settleDelayedDeath(id,tick);if(!died) return died;}
            ports_.monsters.stopScriptedMotion(id);pending.nativeCallbackStarted=false;
        } else {
            // Retail monster Do76 rewinds to 0 with four remaining frames;
            // its hit-count helper consumes one unit-seeded parity draw.
            if(!mode.whirlwindRolled) {
                mode.animation.frame&=255;mode.animation.remaining=4*256;
                const auto expected=source->combatRandom;auto random=expected;const bool hit=(rollRandom(random)&1)==0;
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
                mode.whirlwindHit=hit;mode.whirlwindRolled=true;
            }
            if(mode.whirlwindHit && !mode.whirlwindContact) {
                if(!mode.whirlwindTarget) {
                    const auto candidates=ports_.effects.nativeAuraCandidateRecipients({id,source->area,5,0xa783,false,{},effects::NativeAuraTargetMode::OwnerRange,tick});
                    mode.whirlwindTarget=nativeSkillSuccessor(candidates,mode.whirlwindPrevious,ports_.monsters.nativeIdentities());
                    mode.whirlwindPrevious=*mode.whirlwindTarget?ports_.monsters.nativeIdentities().guid(*mode.whirlwindTarget):UINT32_MAX;
                }
                const auto target=*mode.whirlwindTarget;
                if(target) {
                    if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return {DomainStatus::Capacity,{}};
                    const auto caster=nativeCaster(ports_.players,ports_.monsters,id);if(!caster) return {DomainStatus::InvalidActor,{}};
                    const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
                    const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*stats,*source->rule.stats,*source->rule.formulas);
                    const auto found=ranks.find(pending.request.skill);if(found==ranks.end() || found->second<=0) return {DomainStatus::Unavailable,{}};
                    const auto skill=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,found->second,tick);
                    missiles::Spawn request{caster->context(ports_.areas,tick),skill,{},source->position,true};
                    WeaponSkillDamage weapon;weapon.weapon=source->petWeapon.value_or(WeaponDamage{});weapon.level=caster->level();request.weapon=weapon;
                    if(!source->petWeapon) {
                        request.nativeMeleeContact=std::make_shared<combat::NativeSkillContact>();
                        request.nativeMeleeContact->monsterProgram=pending.attack;
                    }
                    const auto occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);request.nativeMeleeOccurrence=occurrence;
                    const auto contact=ports_.missiles.directContact(request,{target});
                    if(!contact) {--runtime_->nativeKickOccurrence;return {contact.status,{}};}
                    mode.whirlwindContact=occurrence;
                }
            }
            if(mode.whirlwindContact) {
                pending.nativeCallbackStarted=true;
                {auto guard=state_.executingRelease(id);
                    if(ports_.combat.advanceAuraPulse(id,*mode.whirlwindContact,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};}
                mode.whirlwindContact.reset();pending.nativeCallbackStarted=false;
            }
            if(mode.whirlwindHit) mode.whirlwindAlternate=!mode.whirlwindAlternate;
            mode.whirlwindRolled=false;mode.whirlwindTarget.reset();
        }
    }
    source=ports_.monsters.find(id);
    if(!source || !source->alive() || pending.cancelled || source->interruption!=mode.interruption) {cancel({},id);return done();}
    mode.step=advanceMonsterAnimation(mode.animation,mode.program.animation->frames,mode.program.animation->speed,mode.program.animationStartFrame,true,mode.program.animationFlags);
    mode.lastFrame=tick;mode.advanced=true;ports_.monsters.extendAttack(id,tick+1);return done();
}
}
