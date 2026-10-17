#include "system.hpp"
#include <stdexcept>
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/monsters/act2_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <memory>
namespace d2x::server::ai {
StepStatus System::act2Action(EntityId id,UnitTarget target,Vec point,int size,TickContext tick,Controller &controller,int continuation) {
    const auto &monster=*ports_.monsters.find(id);const auto &area=ports_.areas.at(monster.area);
    if(monster.rule.ai.kind==MonsterAiKind::Duriel && monster.rule.nativeAura && !monster.rightSkill) {
        const auto &aura=*monster.rule.nativeAura;
        const auto assigned=perform(controller,[this,id,aura,tick]{return ports_.effects.assignUnitAura(id,aura,tick.tick,aura.rank);});
        if(isDeferred(assigned.status) || assigned.status==DomainStatus::Stale || assigned.status==DomainStatus::Conflict) return StepStatus::Blocked;
        if(!assigned) throw std::logic_error("Native Duriel right aura assignment is unavailable");
    }
    const bool hasTarget=target.id!=id;
    const auto kind=monster.rule.ai.kind;
    if((kind==MonsterAiKind::BatDemon || kind==MonsterAiKind::Mosquito || kind==MonsterAiKind::SandRaider || kind==MonsterAiKind::Swarm || kind==MonsterAiKind::Scarab || kind==MonsterAiKind::Mummy || kind==MonsterAiKind::GreaterMummy || kind==MonsterAiKind::PantherWoman || kind==MonsterAiKind::PantherJavelin || kind==MonsterAiKind::ClawViperEx) && !hasTarget) point=Vec{}-area.definition.origin;
    const auto shootable=(kind==MonsterAiKind::SandMaggot || kind==MonsterAiKind::PantherJavelin ||
        kind==MonsterAiKind::Summoner || kind==MonsterAiKind::GreaterMummy)?shootableTarget(monster,tick):std::nullopt;
    const int distance=target.id==id?INT32_MAX:monsterAiDistance(point,target.type?size:0,monster.position);
    const bool clear=area.definition.collision.missileSegment(monster.position,point,{4,1});
    if(controller.pursuing && !monster.route.empty() && !monster.burrowed && !monster.airborne) {
        if(monster.movementTarget && tick.tick>=controller.nextPath) {
            const auto destination=ports_.monsters.targetPosition(monster.movementTarget,monster.area);
            const auto moved=destination?ports_.monsters.requestMove({id,{monster.area,area.generation,destination->first},monster.movementTarget,monster.stopDistance,monster.velocityPercent,monster.running,monster.pathSteps}):DomainResult<>{DomainStatus::Unavailable,{}};
            controller.nextPath=tick.tick+18;
            if(!moved) {ports_.monsters.stop(id);controller.pursuing=false;}
        }
        controller.nextDecision=tick.tick+1;return StepStatus::Complete;
    }
    ports_.monsters.stop(id);controller.pursuing=false;
    std::optional<UnitTarget> corpse,heal;
    int supportCount=0;
    if(kind==MonsterAiKind::GreaterMummy) if(const auto *room=area.definition.activation.room(monster.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
                if(ports_.monsters.mummySupportTarget(id,candidate,tick.tick,false,true)) ++supportCount;
                if(ports_.monsters.resurrectionTarget(id,candidate,tick.tick)) corpse=UnitTarget{candidate,0,1};
                if(ports_.monsters.mummySupportTarget(id,candidate,tick.tick,false)) heal=UnitTarget{candidate,0,1};
            }
    bool cold=true;
    if(const auto *p=ports_.players.findActor(target.id)) cold=p->totals.character.fireResist>=p->totals.character.coldResist;
    else if(const auto *m=ports_.monsters.find(target.id)) cold=m->rule.resistances[2]>=m->rule.resistances[4];
    if(kind==MonsterAiKind::Scarab) controller.commanded=!controller.commands.empty() && controller.commands.front().code==1;
    MonsterDecisionInput combat{target.id!=id && meleeDistance(monster.position,monster.rule.size,point,size)<=monster.rule.meleeRange && clear,
        controller.charged,distance,monster.rule.difficulty,controller.random,monster.hitOccurrence!=controller.observedHit,false,
        int(monster.life*100/monster.maximumLife),controller.commanded,controller.alerted,
        ports_.monsters.minionOwner(id)==id,controller.phase,controller.loop,100,false,clear};
    combat.continuation=continuation;combat.hasCommand=!controller.commands.empty();
    combat.canShoot=bool(shootable);combat.shootDistance=shootable?shootable->distance:INT32_MAX;
    Act2DecisionInput input{combat,tick.tick,controller.skillDeadline,controller.novaAt,controller.firewallAt,monster.nestSpawned,monster.burrowed,bool(corpse),bool(heal),cold};
    input.hasTarget=hasTarget;
    input.supportCount=supportCount;input.resurrectionReady=corpse && ports_.monsters.resurrectionPlacement(corpse->id);
    input.attempts=controller.act2Attempts;
    if(kind==MonsterAiKind::SandLeaper && hasTarget) input.leapAvailable=ports_.monsters.canMonsterLeap(id,point);
    if(kind==MonsterAiKind::ClawViper || kind==MonsterAiKind::ClawViperEx) input.chargeAvailable=hasTarget && area.definition.collision.missileSegment(monster.position,point,{0x805,1});
    if(kind==MonsterAiKind::Sarcophagus) input.birthAvailable=ports_.monsters.canSpawnNestChild(id);
    const auto squared=[&](Vec p) {const int x=int(p.x)-int(monster.position.x),y=int(p.y)-int(monster.position.y);return x*x+y*y;};
    input.targetDistanceSquared=squared(point);
    std::optional<Vec> ally;EntityId allyId;int allyDistance=INT32_MAX;
    if(kind==MonsterAiKind::SandRaider || kind==MonsterAiKind::PantherWoman || kind==MonsterAiKind::PantherJavelin) {
        if(const auto *room=area.definition.activation.room(monster.position))
            for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
                for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                    if(candidate==id || !other->alive() || (kind==MonsterAiKind::SandRaider?other->alignment()!=UnitAlignment::Evil:other->rule.baseClass!=monster.rule.baseClass)) continue;
                    const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                    const int metric=squared(at);if(metric<allyDistance) {ally=at;allyId=candidate;allyDistance=metric;}
                }
    } else
    for(const auto &[key,other]:ports_.monsters.read().actors) {
        if(key==id || other.area!=monster.area || other.life<=0 || !area.definition.activation.nearby(monster.position,other.position)) continue;
        const int distanceSquared=squared(other.position);
        if((kind==MonsterAiKind::SandRaider?other.enemyTarget():other.rule.baseClass==monster.rule.baseClass) && distanceSquared<allyDistance) {ally=other.position;allyDistance=distanceSquared;}
    }
    input.hasAlly=bool(ally);input.allyDistanceSquared=ally?allyDistance:0;
    auto result=decideAct2Monster(monster.rule.ai,input);
    auto &decision=result.action;
    controller.random=decision.random;
    if(decision.commandParty) commandParty(monster);
    DomainResult<> accepted{DomainStatus::Applied,std::monostate{}};
    if((kind==MonsterAiKind::Baboon || kind==MonsterAiKind::BatDemon) && result.regenerationEighths>=0) {
        controller.regenerationBonus=ports_.monsters.nativeAiRegeneration(id,result.regenerationEighths,controller.regenerationBonus,tick.tick);
    }
    if(result.color>=0 || result.overlay>=0) {
        accepted=perform(controller,[this,id,color=std::max(0,result.color),overlay=result.overlay,tick]{return ports_.monsters.aiColor(id,color,overlay,tick.tick);});
        if(isDeferred(accepted.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    }
    auto random=decision.random;
    bool actionAttempted=false;
    if(result.pose>=0) accepted=perform(controller,[this,id,pose=uint8_t(result.pose),hold=result.holdPose,wait=kind==MonsterAiKind::BatDemon?decision.waitFrames:0,tick]{return ports_.monsters.aiPose(id,pose,hold,false,tick.tick,wait);});
    if(result.exhaust) accepted=perform(controller,[this,id,life=monster.life,kind,tick]{return kind==MonsterAiKind::MaggotEgg?ports_.monsters.scriptDeath(id,tick.tick):ports_.monsters.damage(id,id,life,tick.tick);});
    else if(result.spawnQueen) {
        accepted=perform(controller,[this,id,point,tick,children=std::make_shared<monsters::SkillChildrenExecution>()]() mutable {
            return ports_.monsters.spawnSkillChildren(id,point,1,false,tick.tick,children);
        });
        // Fn066 enters its delay stage even when the spawn location is blocked.
        if(accepted.status==DomainStatus::Unavailable) accepted={DomainStatus::Applied,std::monostate{}};
    }
    else if(decision.action==MonsterDecisionAction::Special || decision.action==MonsterDecisionAction::Attack) {
        const auto skill=decision.action==MonsterDecisionAction::Special?monster.rule.skillIds.at(size_t(decision.skillSlot)):0;
        auto victim=decision.useShootableTarget && shootable?shootable->unit:target;
        if(monster.rule.ai.kind==MonsterAiKind::GreaterMummy && decision.skillSlot==0 && corpse) victim=*corpse;
        if(monster.rule.ai.kind==MonsterAiKind::GreaterMummy && decision.skillSlot==1 && heal) victim=*heal;
        const auto prepared=monster.rule.skillActions.find(uint16_t(skill));
        const uint8_t mode=skill && prepared!=monster.rule.skillActions.end()?prepared->second.nativeMode:decision.attackMode;
        if(kind==MonsterAiKind::Scarab && !controller.commands.empty()) {controller.commands.pop_front();controller.commanded=false;}
        accepted=requestAction(controller,{id,uint16_t(skill),victim,tick.tick,mode});
    } else if(decision.action==MonsterDecisionAction::Approach) {
        const Vec goal=result.seekAlly?*ally:decision.approachRadius>0?monsterRadiusApproachPoint(monster.position,monster.rule.size,point,decision.approachRadius):point;
        controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},result.seekAlly?allyId:decision.approachRadius>0 || !hasTarget?EntityId{}:target.id,result.seekAlly?0:decision.stopDistance,decision.velocityPercent,decision.running,decision.pathSteps,0,0,decision.pathType,uint8_t(decision.pathDistance)}));
        if(!controller.pursuing && (kind==MonsterAiKind::Baboon || kind==MonsterAiKind::PinHead || kind==MonsterAiKind::SandLeaper || kind==MonsterAiKind::ClawViper || kind==MonsterAiKind::ClawViperEx || kind==MonsterAiKind::Swarm || kind==MonsterAiKind::Mummy || kind==MonsterAiKind::PantherWoman || (kind==MonsterAiKind::PantherJavelin && result.seekAlly) || (kind==MonsterAiKind::Scarab && result.scarabPhaseAfterMove))) {
            if(rollRandom(random)%100>=70) decision.waitFrames=10;
            else controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,monsterWanderPoint(monster.position,4,random)},{},0,75,false,1}));
        }
        if(result.scarabPhaseAfterMove && limitedRandom(random,100)>10) decision.phase=0;
        if(kind==MonsterAiKind::Scarab && !controller.pursuing && controller.commanded && !controller.commands.empty()) {controller.commands.pop_front();controller.commanded=false;}
        controller.nextPath=tick.tick+18;
    } else if(decision.action==MonsterDecisionAction::Circle || decision.action==MonsterDecisionAction::Retreat || decision.action==MonsterDecisionAction::Wander) {
        if(decision.action==MonsterDecisionAction::Circle) {
            const int turn=(rollRandom(random)&255)<128?1:-1;
            controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,point},target.id,0,decision.velocityPercent,false,0,turn,decision.stopDistance}));
        } else {
            const Vec goal=decision.action==MonsterDecisionAction::Retreat?monsterRetreatPoint(monster.position,point,decision.stopDistance):monsterWanderPoint(decision.wanderAroundTarget?point:monster.position,decision.stopDistance,random);
            const bool escape=(kind==MonsterAiKind::Baboon || kind==MonsterAiKind::BatDemon || kind==MonsterAiKind::Mosquito || kind==MonsterAiKind::PantherJavelin) && decision.action==MonsterDecisionAction::Retreat;
            controller.pursuing=!((kind==MonsterAiKind::BatDemon || kind==MonsterAiKind::Mosquito || kind==MonsterAiKind::SandRaider) && !hasTarget && escape) && bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,decision.velocityPercent,decision.running,escape || kind==MonsterAiKind::SandLeaper || kind==MonsterAiKind::Mosquito || decision.pathSteps>0?std::max(1,decision.pathSteps):0,0,0,uint8_t(escape && (kind==MonsterAiKind::Baboon || kind==MonsterAiKind::Mosquito)?2:13),uint8_t(escape?decision.stopDistance:5)}));
        }
        if(!controller.pursuing && decision.failedEscapeContinuation && kind==MonsterAiKind::BatDemon) {
            controller.random=random;
            return act2Action(id,target,point,size,tick,controller,decision.failedEscapeContinuation);
        }
        if(!controller.pursuing && decision.attackWhenMoveFails) {
            const uint8_t mode=kind==MonsterAiKind::Baboon?(int(limitedRandom(random,100))<monster.rule.ai.params[3]?4:5):decision.failedMoveAttackMode;
            if(controller.randomPublished && random!=decision.random) {
                const auto consumed=ports_.monsters.consumeRandom(id,monster.combatRandom,random);if(!consumed) return StepStatus::Blocked;
            }
            controller.random=random;accepted=requestAction(controller,{id,0,target,tick.tick,mode});actionAttempted=true;
        }
        if(!controller.pursuing && decision.failedMoveWanderRadius) controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,monsterWanderPoint(monster.position,decision.failedMoveWanderRadius,random)},{},0,kind==MonsterAiKind::Baboon?75:decision.velocityPercent,kind==MonsterAiKind::Baboon?false:decision.running}));
        if(result.idleAfterMove) {ports_.monsters.stop(id);controller.pursuing=false;}
    }
    if(result.postColor>=0) {
        const auto colored=perform(controller,[this,id,color=result.postColor,tick] {return ports_.monsters.aiColor(id,color,-1,tick.tick);});
        if(isDeferred(colored.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    }
    if(isDeferred(accepted.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    if(random!=decision.random && !controller.randomPublished) controller.random=random;
    else if(random!=decision.random && !actionAttempted) {
        const auto consumed=ports_.monsters.consumeRandom(id,monster.combatRandom,random);
        if(!consumed) return StepStatus::Blocked;
    }
    controller.random=random;
    if(accepted || kind==MonsterAiKind::Scarab || kind==MonsterAiKind::SandRaider || kind==MonsterAiKind::Baboon || kind==MonsterAiKind::BatDemon || kind==MonsterAiKind::PinHead || kind==MonsterAiKind::ClawViper || kind==MonsterAiKind::ClawViperEx || kind==MonsterAiKind::MaggotLarva || kind==MonsterAiKind::MaggotEgg || kind==MonsterAiKind::SandMaggot || kind==MonsterAiKind::Mosquito) {
        controller.phase=decision.phase;controller.loop=decision.loop;controller.charged=decision.charged;controller.skillDeadline=result.deadline;controller.novaAt=result.novaAt;controller.firewallAt=result.firewallAt;controller.act2Attempts=result.attempts;
        if(result.regenerationEighths>=0 && kind!=MonsterAiKind::Baboon && kind!=MonsterAiKind::BatDemon) ports_.monsters.aiRegeneration(id,result.regenerationEighths);
    }
    if(kind==MonsterAiKind::Scarab) controller.charged=false;
    controller.observedHit=monster.hitOccurrence;controller.nextDecision=tick.tick+uint64_t(result.overlay>=0?monster.rule.decisionTicks+1:controller.pursuing?1:std::max(1,decision.waitFrames));
    return StepStatus::Complete;
}
}
