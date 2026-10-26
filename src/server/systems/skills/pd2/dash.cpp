#include "../system.hpp"
#include "../runtime.hpp"
#include "../native_caster.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::skills {
DomainResult<> System::pd2DashEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto id=event.owner.unit;auto clock=runtime_->monsterModes.find(id);auto release=runtime_->monsters.find(id);
    if(clock==runtime_->monsterModes.end() || release==runtime_->monsters.end()) {cancel({},id);return done();}
    auto &mode=clock->second;auto &pending=release->second;
    if(!mode.pd2Dash || !pending.launch) {cancel({},id);return done();}
    auto &dash=*mode.pd2Dash;
    if(mode.advanced && mode.lastFrame==tick) return done();
    auto caster=nativeCaster(ports_.players,ports_.monsters,id);
    if(!caster || !caster->monster || !caster->programs()) {cancel({},id);return done();}
    const auto &program=*caster->programs()->definitions.at(pending.request.skill).spec.pd2;
    if(!dash.movingState) {
        SkillCastSpec state;state.sourceId=pending.request.skill;state.rank=pending.attack.rank;
        CombatEffectSpec effect;effect.state=caster->monster->rule.states->at(18);effect.nativeState=true;
        effect.nativeValues.emplace();effect.modeActionRevision=caster->monster->nativeActionRevision;
        state.appliedEffect=std::move(effect);
        const auto changed=ports_.effects.unitSkill(id,state,tick);if(!changed) return changed;
        dash.movingState=true;
    }
    if(!pending.nativeCallbackStarted && (!mode.movementAdvanced || mode.movementFrame!=tick)) {
        if(dash.flags&0x100) ports_.monsters.advanceScriptedMotion(id,tick);
        mode.movementAdvanced=true;mode.movementFrame=tick;
    }
    const bool action=mode.step.event==1 || mode.step.event==2 || mode.step.event==4;
    if(pending.nativeCallbackStarted || action) {
        postPd2MonsterCastItemEvent(pending,tick);
        if(!pending.nativeCallbackStarted) {
            dash.callbackPosition=caster->position();
            const auto at=dash.callbackPosition;
            dash.callbackSlot=!(dash.flags&0x100)?0:
                std::abs(int(at.x)-int(pending.launch->second.x))<=2 && std::abs(int(at.y)-int(pending.launch->second.y))<=2?1:2;
        }
        pending.nativeCallbackStarted=true;
        const auto position=dash.callbackPosition;
        const bool starting=dash.callbackSlot==0,arriving=dash.callbackSlot==1;
        if(starting && !dash.motionStarted) {
            const auto moved=ports_.monsters.startPd2DashMotion(id,pending.launch->second);if(!moved) return moved;
            dash.motionStarted=true;
        } else if(arriving && !dash.landed) {
            ports_.monsters.stopScriptedMotion(id);
            const auto &area=ports_.areas.at(caster->area());const auto global=position+area.definition.origin;
            const auto room=ports_.areas.roomAtPosition(caster->area(),position,int(global.x),int(global.y));
            if(room) if(const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),caster->monster->rule.size,
                0x3c01,ports_.spatial.nativeCollisionSample(0x3c01))) {
                const auto &destination=ports_.areas.at(free->room.area);
                const auto landed=ports_.monsters.teleport(id,{free->room.area,destination.generation,free->position},tick,0,true);
                if(!landed) return landed;
            }
            const auto removed=ports_.effects.endMode(id,tick);if(!removed) return removed;
            dash.landed=true;
        }
        if(!dash.birth) {
            std::vector<missiles::NativeSpawn> births;
            const int slot=dash.callbackSlot,definition=program.missiles[size_t(slot)];
            if(definition>=0) {
                int count=1;
                if(slot!=2) {
                    auto input=caster->formulaInput(ports_.effects,tick);SkillFormulaValues values(*caster->programs()->formulas,input);
                    const auto calculated=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,slot==0?"calc1":"calc2");
                    if(!calculated) return {DomainStatus::Unavailable,{}};
                    count=*calculated;
                }
                if(slot==2) births.push_back({id,caster->area(),ports_.areas.at(caster->area()).generation,tick,position,{},
                    definition,pending.request.skill,pending.attack.rank});
                else if(count>0) for(int direction=0,step=64/std::min(count,64);direction<64;direction+=step)
                    births.push_back({id,caster->area(),ports_.areas.at(caster->area()).generation,tick,position,
                        position+missileRingDirection(direction),definition,pending.request.skill,pending.attack.rank});
            }
            if(!births.empty()) {
                const auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
                dash.birth=*prepared.value;
            }
        }
        if(dash.birth) {const auto published=ports_.missiles.commitNativeBirth(dash.birth);if(!published) return {published.status,{}};}
        dash.birth.reset();pending.nativeCallbackStarted=false;pending.pd2CastEventPosted=false;mode.step.event=0;
        if(starting) dash.flags=0x1101;
        else if(arriving) {dash.flags=0;mode.animation.remaining=0;}
        else {mode.animation.frame&=255;mode.animation.remaining=0x600;}
    }
    if(mode.animation.remaining<=0) {
        ports_.monsters.stopScriptedMotion(id);
        const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
        runtime_->monsters.erase(release);state_.interrupt(id,tick);ports_.monsters.finishAction(id,tick);
        ports_.frameEvents.cancel(event.handle);runtime_->monsterModes.erase(clock);ports_.monsters.resumeMode(id,tick);return done();
    }
    mode.step=advanceMonsterAnimation(mode.animation,mode.program.animation->frames,mode.program.animation->speed,
        mode.program.animationStartFrame,true,mode.program.animationFlags);
    mode.lastFrame=tick;mode.advanced=true;ports_.monsters.extendAttack(id,tick+1);return done();
}
}
