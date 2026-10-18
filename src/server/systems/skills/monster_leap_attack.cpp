#include "system.hpp"
#include "runtime.hpp"
#include "native_caster.hpp"
#include "evaluation.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::skills {
DomainResult<> System::startMonsterLeapAttack(const CastRequest &request,const MonsterAttackRule &program) {
    const auto *source=ports_.monsters.find(request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
    if(source->nativeActionRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto [entry,fresh]=runtime_->monsterLeapAttackStarts.try_emplace(request.actor);
    auto &start=entry->second;
    if(fresh || start.tick!=request.tick || start.skill!=request.skill) start={request.tick,request.skill};
    if(!start.stage) {
        const auto landing=ports_.monsters.leapAttackLanding(source->id,std::get<UnitTarget>(request.target).id,ports_.spatial.nativeCollisionSample(0x1c09));
        if(!landing) return {DomainStatus::Unavailable,{}};
        start.landing=*landing;
        const auto reserved=ports_.monsters.reserveLeapAttack(source->id,*landing);if(!reserved) return reserved;
        ++start.stage;
    }
    while(start.stage<=2) {
        source=ports_.monsters.find(request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
        const int state=start.stage==1?source->rule.uninterruptableState:source->rule.skillMoveState;
        if(state<0 || !source->rule.states->contains(state)) return {DomainStatus::Unavailable,{}};
        SkillCastSpec skill;skill.sourceId=request.skill;skill.rank=program.rank;
        CombatEffectSpec effect;effect.state=source->rule.states->at(state);effect.nativeState=true;effect.nativeDirectAllocation=true;
        effect.nativeValues.emplace();effect.modeActionRevision=source->nativeActionRevision+1;skill.appliedEffect=std::move(effect);
        const auto applied=ports_.effects.unitSkill(request.actor,skill,request.tick);if(!applied) return applied;
        ++start.stage;
    }
    return {DomainStatus::Applied,std::monostate{}};
}

DomainResult<> System::monsterLeapAttackEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto id=event.owner.unit;auto clock=runtime_->monsterModes.find(id);auto release=runtime_->monsters.find(id);
    if(clock==runtime_->monsterModes.end() || release==runtime_->monsters.end()) {cancel({},id);return done();}
    auto &mode=clock->second;auto &pending=release->second;auto &leap=*mode.leapAttack;
    if(mode.advanced && mode.lastFrame==tick) return done();
    if(mode.animation.remaining<=0 || (leap.endAt && tick>=leap.endAt)) {
        pending.nativeCallbackStarted=true;
        {auto guard=state_.executingRelease(id);
            const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
            const auto died=ports_.monsters.settleDelayedDeath(id,tick);if(!died) return died;}
        ports_.monsters.stopScriptedMotion(id);runtime_->monsters.erase(release);state_.interrupt(id,tick);
        ports_.monsters.finishAction(id,tick);ports_.frameEvents.cancel(event.handle);runtime_->monsterModes.erase(clock);ports_.monsters.resumeMode(id,tick);return done();
    }
    const auto *source=ports_.monsters.find(id);if(!source) {cancel({},id);return done();}
    if((leap.flags&0x100) && (!mode.movementAdvanced || mode.movementFrame!=tick)) {
        leap.landed=!ports_.monsters.advanceScriptedMotion(id,tick);
        const auto origin=ports_.areas.at(source->area).definition.origin;
        const auto from=source->position+origin,to=leap.landing+origin;
        leap.landed|=int(std::floor(from.x))==int(to.x) && int(std::floor(from.y))==int(to.y);
        mode.movementFrame=tick;mode.movementAdvanced=true;
    }
    const auto findTarget=[&]() ->EntityId {
        // The original saved GUID remains eligible even outside melee range.
        // Combat's result-flags gate decides whether that contact can hit.
        const auto saved=std::get<UnitTarget>(pending.request.target).id;
        if(ports_.players.findActor(saved) || ports_.monsters.find(saved)) return saved;
        const auto candidates=ports_.effects.nativeAuraCandidateRecipients({id,source->area,source->rule.meleeRange+4,0x2a783,false,{},effects::NativeAuraTargetMode::OwnerRange,tick});
        return nativeSkillSuccessor(candidates,UINT32_MAX);
    };
    if(pending.nativeCallbackStarted || ((leap.flags&0x100) && leap.landed) || mode.step.event==1 || mode.step.event==2 || mode.step.event==4) {
        if(leap.flags&0x100) {
            if(leap.landed) {
                if(!leap.statesCleared) {
                    pending.nativeCallbackStarted=true;
                    {auto guard=state_.executingRelease(id);
                        const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
                        const auto died=ports_.monsters.settleDelayedDeath(id,tick);if(!died) return died;}
                    leap.statesCleared=true;
                }
                const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},source->area},{SkillEndFact{id,1,source->area,pending.request.skill}}});
                if(!sent) return {sent.status,{}};
                ports_.monsters.stopScriptedMotion(id);pending.nativeCallbackStarted=false;
                leap.flags=0x200;mode.animation.remaining=(mode.animation.remaining&~255)-256;
                if(!findTarget()) leap.endAt=tick+4;
                else {
                    int frames=0;
                    for(int point=16*256+mode.program.animation->speed;point<mode.program.animation->frames*256;point+=mode.program.animation->speed) ++frames;
                    leap.endAt=tick+uint64_t(std::max(1,frames))+1;
                }
                // RewindSkillEx(16) reschedules timers; it does not set the
                // sequence cursor to frame16. StopSequence keeps its cursor.
            } else {
                const auto from=source->position+ports_.areas.at(source->area).definition.origin;
                const auto to=leap.landing+ports_.areas.at(source->area).definition.origin;
                const int distance=unitCoordinateDistance(int(std::floor(from.x)),int(std::floor(from.y)),int(to.x),int(to.y));
                mode.animation.frame=(mode.animation.frame&255)+(distance<2?12:10)*256;
                mode.animation.remaining=(mode.animation.remaining&~255)+256;
            }
        } else if(leap.flags&0x80) {
            const auto started=ports_.monsters.startLeapAttackMotion(id);if(!started) return started;
            leap.flags=0x1101;
        } else if(leap.flags&0x200) {
            if(!leap.target) leap.target=findTarget();
            if(*leap.target && !leap.contact) {
                if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return {DomainStatus::Capacity,{}};
                const auto caster=nativeCaster(ports_.players,ports_.monsters,id);if(!caster) return {DomainStatus::InvalidActor,{}};
                const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
                const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*stats,*source->rule.stats,*source->rule.formulas);
                const auto rank=ranks.find(pending.request.skill);if(rank==ranks.end() || rank->second<=0) return {DomainStatus::Unavailable,{}};
                const auto skill=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,rank->second,tick);
                missiles::Spawn request{caster->context(ports_.areas,tick),skill,{},source->position,true};
                WeaponSkillDamage weapon;weapon.weapon=source->petWeapon.value_or(WeaponDamage{});weapon.level=caster->level();request.weapon=weapon;
                const auto occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);request.nativeMeleeOccurrence=occurrence;
                const auto contact=ports_.missiles.directContact(request,{*leap.target});
                if(!contact) {--runtime_->nativeKickOccurrence;return {contact.status,{}};}
                leap.contact=occurrence;
            }
            if(leap.contact) {
                pending.nativeCallbackStarted=true;
                {auto guard=state_.executingRelease(id);
                    if(ports_.combat.advanceAuraPulse(id,*leap.contact,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};}
                leap.contact.reset();pending.nativeCallbackStarted=false;
            }
            leap.target.reset();
        } else leap.flags=0x1000;
    }
    source=ports_.monsters.find(id);
    if(!source || !source->alive() || pending.cancelled || source->interruption!=mode.interruption) {cancel({},id);return done();}
    mode.step=advanceMonsterAnimation(mode.animation,mode.program.animation->frames,mode.program.animation->speed,mode.program.animationStartFrame,true,mode.program.animationFlags);
    mode.lastFrame=tick;mode.advanced=true;ports_.monsters.extendAttack(id,tick+1);return done();
}
}
