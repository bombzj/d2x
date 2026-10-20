#include "system.hpp"
#include "runtime.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/monsters/mode_stats.hpp"
#include <stdexcept>

namespace d2x::server::skills {
DomainResult<Vec> System::startMonsterLeap(const CastRequest &request) {
    const auto target=std::get<UnitTarget>(request.target).id;
    auto &cached=runtime_->monsterLeapStarts[request.actor];
    const auto *source=ports_.monsters.find(request.actor);
    if(!source || !source->alive()) return {DomainStatus::InvalidActor,{}};
    if(!cached || cached->tick!=request.tick || cached->target!=target) {
        if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return {DomainStatus::Capacity,{}};
        const auto position=ports_.monsters.targetPosition(target,source->area);if(!position) return {DomainStatus::Unavailable,{}};
        cached=std::make_shared<Runtime::MonsterLeapStart>();cached->tick=request.tick;cached->target=target;
        cached->targetPosition=position->first;cached->occurrence=(uint64_t{1}<<54)|(++runtime_->nativeKickOccurrence);
        cached->attack=source->rule.attacks.at(4);
    }
    const auto start=cached;
    if(!start->stage) {
        auto random=source->combatRandom;
        start->attack.modeStats=prepareMonsterModeStats(*source->rule.modeTables.at(size_t(source->rule.difficulty)),*source->rule.stats,
            {4,source->rule.level,source->rule.difficulty,source->rule.statGameType,source->admittedPlayerCount,
             source->alignment()==UnitAlignment::Evil,{},source->id,request.tick},random);
        const auto applied=ports_.monsters.attackProfile(source->id,start->attack.modeStats,source->combatRandom,random);
        if(!applied) return {applied.status,{}};
        ++start->stage;
    }
    if(start->stage==1) {
        const auto contact=ports_.combat.monsterMelee(source->id,target,start->attack,start->occurrence,{request.tick});
        if(!contact) return {contact.status,{}};
        if(!source->alive()) return {DomainStatus::InvalidActor,{}};
        // A synchronous reflected contact may cancel caches while St40 is on
        // the stack. Keep this completed start across output retries.
        runtime_->monsterLeapStarts.insert_or_assign(source->id,start);++start->stage;
    }
    if(start->stage==2) {
        if(const auto point=ports_.monsters.targetPosition(target,source->area)) start->targetPosition=point->first;
        const auto landing=ports_.monsters.leapLanding(source->id,start->targetPosition*2.f-source->position);
        if(!landing) return {DomainStatus::Unavailable,{}};
        start->landing=*landing;
        ++start->stage;
    }
    if(start->stage==3) {
        const auto changed=ports_.monsters.toggleAiState(source->id,source->rule.uninterruptableState,true,request.tick);
        if(!changed) return {changed.status,{}};
        ++start->stage;
    }
    if(start->stage==4) {
        const auto changed=ports_.monsters.toggleAiState(source->id,source->rule.skillMoveState,true,request.tick);
        if(!changed) return {changed.status,{}};
        ++start->stage;
    }
    return {DomainStatus::Applied,start->landing};
}
DomainResult<bool> System::releaseMonsterLeap(MonsterRelease &pending,TickContext tick) {
    const auto *source=ports_.monsters.find(pending.request.actor);
    if(!source || !source->alive() || !pending.leapDestination) return {DomainStatus::Applied,true};
    if(!pending.hitReleased) {
        const auto started=ports_.monsters.startSkillMotion(source->id,*pending.leapDestination,tick.tick+1,pending.request.skill,true,tick.tick);
        if(!started) return {started.status,{}};
        pending.hitReleased=true;pending.due=tick.tick+1;return {DomainStatus::Applied,false};
    }
    auto &mode=runtime_->monsterModes.at(source->id);
    if(source->skillMotion) {
        mode.animation.frame=(mode.animation.frame&255)+8*256;
        mode.animation.remaining+=256;mode.step.ended=false;
        pending.due=tick.tick+1;return {DomainStatus::Applied,false};
    }
    const auto landed=ports_.monsters.finishLeaperLanding(source->id,std::get<UnitTarget>(pending.request.target).id,pending.request.skill,tick.tick);
    if(!landed) return {landed.status,{}};
    mode.animation.frame=(mode.animation.frame&255)+12*256;
    mode.animation.remaining+=256;mode.step.ended=false;
    return {DomainStatus::Applied,true};
}
}
