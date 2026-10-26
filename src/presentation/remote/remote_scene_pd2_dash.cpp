#include "remote_scene_impl.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void RemoteScene::Impl::advancePd2Dashes(const Grid &collision,Vec origin,SceneView &shared,const RemoteCombat &combat) {
    for(const auto &[key,unit]:world().units) {
        if(key.type!=1 || !unit.position || !unit.actionSkill || onlineMonsterCorpse(unit)) continue;
        const auto skill=skillRows.find(*unit.actionSkill),monster=monsterRows.find(unitClass(unit).value_or(UINT16_MAX));
        if(skill==skillRows.end() || monster==monsterRows.end() || skills.number(skill->second,"cltdofunc")!=114) continue;
        const auto sequence=monsterSequenceFlags.find(monsterSkillMode(monster->second,*unit.actionSkill));
        if(sequence==monsterSequenceFlags.end() || sequence->second.empty()) continue;
        auto &motionState=motion[key];
        if(!motionState.scriptedSequence || motionState.scriptedSequence->revision!=unit.actionRevision) {
            const auto target=unit.destination?std::optional{Vec{float(unit.destination->x),float(unit.destination->y)}}:
                unit.destinationUnit?localUnitPosition(*unit.destinationUnit):std::nullopt;
            if(!target) continue;
            if(!motionState.last) {motionState.position={float(unit.position->x),float(unit.position->y)};motionState.last=unit.position;}
            Motion::ScriptedSequence run;run.revision=unit.actionRevision;run.nativeFrame=true;
            run.destination=*target;run.animation.remaining=int(sequence->second.size())*256;run.leapFlags=0x80;
            run.velocity=float(uint32_t(monstats.number(monster->second,"Velocity").value_or(0))<<9)*25.f/4096.f;
            run.nextTick=time+NativePathStep::seconds;
            motionState.scriptedSequence=std::move(run);motionState.pathStep.reset();motionState.route.clear();
            motionState.goal.reset();motionState.correction={};motionState.correctionLeft=0;
            missileCastRevisions[key]=unit.actionRevision;
        }
        auto &run=*motionState.scriptedSequence;
        const auto frozen=combat.states().find(key);
        if(frozen!=combat.states().end() && frozen->second.decoded &&
           std::any_of(frozen->second.states.begin(),frozen->second.states.end(),[](const auto &state){return state.second.name=="freeze";})) {
            run.nextTick=time+NativePathStep::seconds;continue;
        }
        const auto emit=[&](int slot,std::string_view formula) {
            const auto missile=missileNames.find(skills.value(skill->second,std::array{"cltmissilea","cltmissileb","cltmissilec"}[size_t(slot)]));
            if(missile==missileNames.end()) return;
            const int level=unit.actionSkillLevel.value_or(1),id=missiles.number(missile->second,"Id").value_or(-1);
            const auto at=motionState.position+Vec{.5f,.5f};
            const auto launch=[&](Vec target) {shared.launchClientMissile(id,at,target,level,run.nextTick-time,{},-1,
                effectOwner(key),combat.hostileSource(unit),0,ClientMissileSource::Cast);};
            if(slot==2) {launch(at);return;}
            const auto count=skillCalculation(*unit.actionSkill,level,formula,key);
            if(!count || *count<1) return;
            for(int direction=0,step=64/std::min(*count,64);direction<64;direction+=step)
                launch(at+missileRingDirection(direction));
        };
        const auto grid=navigationCollision(collision,origin,key);
        while(!run.ended && run.nextTick<=time+1e-5f) {
            run.nextTick+=NativePathStep::seconds;
            if(run.animation.remaining<=0) {run.ended=true;run.moving=false;break;}
            if(run.moving) {
                const Vec center{.5f,.5f};
                const auto step=motionState.pathStep.advance(grid,motionState.position-origin+center,
                    run.destination-origin+center,run.velocity*NativePathStep::seconds,{0,unitSize(unit)});
                motionState.look=run.destination-motionState.position;motionState.position=step.position+origin-center;
            }
            if(run.event==1 || run.event==2 || run.event==4) {
                if(!(run.leapFlags&0x100)) {
                    run.leapFlags=0x1101;run.moving=true;emit(0,"cltcalc1");
                } else if(std::abs(int(motionState.position.x)-int(run.destination.x))<=2 &&
                          std::abs(int(motionState.position.y)-int(run.destination.y))<=2) {
                    run.moving=false;run.leapFlags=0;run.animation.frame=10*256;run.animation.remaining=256;emit(1,"cltcalc2");
                } else {
                    if((run.animation.frame&~255)>0x5ff) {run.animation.frame&=255;run.animation.remaining=0x700;}
                    emit(2,{});
                }
            }
            const auto step=advanceMonsterAnimation(run.animation,int(sequence->second.size()),256,0,true,sequence->second);
            run.event=step.event;run.ended=step.ended;
        }
        motionState.actionRevision=unit.actionRevision;
    }
}
}
