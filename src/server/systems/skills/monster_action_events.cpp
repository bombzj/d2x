#include "system.hpp"
#include "runtime.hpp"
#include "weapon_melee.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::skills {
DomainResult<> System::monsterActionEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    auto clock=runtime_->monsterModes.find(event.owner.unit);
    if(clock==runtime_->monsterModes.end() || clock->second.identity!=event.payload) {
        ports_.frameEvents.cancel(event.handle);return done();
    }
    if(tick<event.due) return done();
    const auto *unit=ports_.monsters.find(event.owner.unit);
    const auto pending=runtime_->monsters.find(event.owner.unit);
    const bool continuing=pending!=runtime_->monsters.end() && pending->second.nativeCallbackStarted;
    if(!unit || (!continuing && (!unit->alive() || unit->interruption!=clock->second.interruption))) {
        cancel({},event.owner.unit);ports_.frameEvents.cancel(event.handle);return done();
    }
    if(!continuing && (ports_.monsters.frozen(unit->id,tick) || unit->stunnedUntil>tick || unit->knockedUntil>tick)) return done();
    if(!continuing && !ports_.areas.loaded(unit->area,unit->position)) {ports_.monsters.extendAttack(unit->id,tick+1);return done();}
    // Native monster Inferno replaces MODECHANGE with its next callback.
    // A retained continuous dispatcher must not advance animation between
    // callbacks, or emit additional releases while waiting for ENDANIM.
    if(pending!=runtime_->monsters.end() && pending->second.attack.action==MonsterAttackRule::Action::Inferno &&
       pending->second.hitReleased && tick<pending->second.due) return done();
    auto &mode=clock->second;const auto &program=mode.program;
    if(!mode.advanced || mode.lastFrame!=tick) {
        const auto *weapon=unit->petWeapon?&*unit->petWeapon:nullptr;
        const auto sampled=ports_.effects.unitStatValues(unit->id,tick,weapon?std::optional{weapon->item}:std::nullopt);
        if(!sampled || !unit->rule.stats) return {DomainStatus::Unavailable,{}};
        const auto stat=[&](std::string_view name){return logicalStatValue(*sampled,*unit->rule.stats,name);};
        auto rate=nativeSubtract(stat("attackrate"),100);
        if(unit->assassinPet && unit->assassinPet->definition->warp && unit->rule.formulas->skills.at(mode.skill).number("cltstfunc")==9)
            rate=nativeAdd(rate,unit->rule.formulas->skills.at(mode.skill).number("param4"));
        const int speed=program.casting?effectiveCastSpeed(program.animation->speed,stat("item_fastercastrate")):
            program.useAttackRate?effectiveAttackSpeed(program.animation->speed,stat("item_fasterattackrate"),weapon?weapon->baseSpeed:0,rate):
            program.animationSequence?program.animation->speed:effectiveOtherAnimationSpeed(program.animation->speed,nativeSubtract(stat("other_animrate"),100));
        mode.step=mode.frozenSequence?MonsterAnimationStep{}:advanceMonsterAnimation(mode.animation,program.animation->frames,speed,program.animationStartFrame,
            program.animationSequence,program.animationFlags);
        mode.lastFrame=tick;mode.advanced=true;
    }
    if(program.action==MonsterAttackRule::Action::Submerge && (mode.animation.frame>>8)<=0) {
        // Do94 clears the action flag and stops the sequence at frame zero.
        // Its AI timer keeps running and may replace this held mode.
        mode.frozenSequence=true;mode.step={};mode.animation.remaining=256;
        runtime_->monsters.erase(event.owner.unit);
    }
    bool scripted=false;
    const bool inferno=program.action==MonsterAttackRule::Action::Inferno;
    const bool actionFlag=mode.step.event==program.animationEvent;
    if(!runtime_->monsters.contains(event.owner.unit) && actionFlag && !mode.step.ended && mode.repeated)
        runtime_->monsters.emplace(event.owner.unit,*mode.repeated);
    const auto current=runtime_->monsters.find(event.owner.unit);
    if(current!=runtime_->monsters.end()) {
        const auto &release=current->second;
        scripted=release.attack.action==MonsterAttackRule::Action::Charge || (release.attack.action==MonsterAttackRule::Action::Leap && release.hitReleased) ||
            release.attack.action==MonsterAttackRule::Action::SwarmMove || release.attack.action==MonsterAttackRule::Action::Animation || (inferno && release.hitReleased);
        scripted|=release.attack.action==MonsterAttackRule::Action::Skill &&
            unit->rule.formulas->skills.at(release.request.skill).number("srvdofunc")==19;
        if(release.nativeSkill) {
            const auto &skill=release.nativeSkill->skill;
            scripted|=bool(release.nativeSkill->assassinKick) ||
                (skill.assassin && skill.assassin->program==AssassinProgram::BladeFury) ||
                (skill.druid && skill.druid->program==DruidProgram::Arctic);
        }
        if(continuing || scripted || (!mode.step.ended && actionFlag)) {
            if(!scripted) current->second.due=tick;
            if(releaseMonsters({tick},event.owner.unit)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        }
    }
    // A synchronous contact can interrupt or replace its source's mode.
    clock=runtime_->monsterModes.find(event.owner.unit);
    if(clock==runtime_->monsterModes.end() || clock->second.identity!=event.payload) return done();
    if(inferno && !runtime_->monsters.contains(event.owner.unit)) clock->second.step.ended=true;
    if(clock->second.step.ended && (!scripted || !runtime_->monsters.contains(event.owner.unit))) {
        const auto ended=ports_.effects.endMode(event.owner.unit,tick);if(!ended) return ended;
        if(const auto start=runtime_->weaponStarts.find(event.owner.unit);start!=runtime_->weaponStarts.end() &&
           start->second.skill.weapon && start->second.skill.weapon->doFunction==83) {
            if(start->second.weaponMelee && start->second.weaponMelee->occurrence)
                ports_.combat.discardNativeMelee(event.owner.unit,*start->second.weaponMelee->occurrence);
            runtime_->weaponStarts.erase(start);
        }
        const auto release=runtime_->monsters.find(event.owner.unit);
        if(release!=runtime_->monsters.end()) cancel({},event.owner.unit);
        ports_.monsters.finishAction(event.owner.unit,tick);
        ports_.frameEvents.cancel(event.handle);runtime_->monsterModes.erase(event.owner.unit);
        ports_.monsters.resumeMode(event.owner.unit,tick);
    } else if(unit && unit->alive() && !clock->second.frozenSequence) ports_.monsters.extendAttack(event.owner.unit,tick+1);
    return done();
}
}
