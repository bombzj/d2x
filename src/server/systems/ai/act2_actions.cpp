#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/monsters/act2_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::ai {
StepStatus System::act2Action(EntityId id,UnitTarget target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &monster=*ports_.monsters.find(id);const auto &area=ports_.areas.at(monster.area);
    const bool hasTarget=target.id!=id;
    const auto kind=monster.rule.ai.kind;
    // Native shootable-target callbacks operate independently of primary AI
    // acquisition. Keep the nearest clear victim for maggot spit and casters.
    if(kind==MonsterAiKind::SandMaggot || kind==MonsterAiKind::PantherJavelin || kind==MonsterAiKind::Summoner || kind==MonsterAiKind::GreaterMummy) {
        int nearest=INT32_MAX;
        const combat::Participants participants{ports_.players,ports_.monsters};
        const auto candidate=[&](UnitTarget victim,Vec position,int extent) {
            const int distance=monsterAiDistance(position,0,monster.position);
            if(distance<nearest && area.definition.activation.nearby(position,monster.position) &&
               participants.canHarm(id,victim.id) && area.definition.collision.missileSegment(monster.position,position,{4,1})) {
                nearest=distance;target=victim;point=position;size=extent;
            }
        };
        for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==monster.area && p.persistent.player.hp>0) candidate({p.actor,0,0},p.position,2);}
        for(const auto &[key,m]:ports_.monsters.read().actors) if(m.area==monster.area && m.life>0 && m.damageable()) candidate({key,0,1},m.position,m.rule.size);
    }
    const int distance=target.id==id?INT32_MAX:monsterAiDistance(point,target.type?size:0,monster.position);
    const bool clear=area.definition.collision.missileSegment(monster.position,point,{4,1});
    if(controller.pursuing && !monster.route.empty() && !monster.burrowed && !monster.airborne) {controller.nextDecision=tick.tick+1;return StepStatus::Complete;}
    ports_.monsters.stop(id);controller.pursuing=false;
    std::optional<UnitTarget> corpse,heal;
    if(monster.rule.ai.kind==MonsterAiKind::GreaterMummy) for(const auto &[key,other]:ports_.monsters.read().actors) {
        (void)other;if(ports_.monsters.resurrectionTarget(id,key,tick.tick)) corpse=UnitTarget{key,0,1};
        if(ports_.monsters.mummySupportTarget(id,key,tick.tick,false)) heal=UnitTarget{key,0,1};
    }
    bool cold=true;
    if(const auto *p=ports_.players.findActor(target.id)) cold=p->totals.character.fireResist>=p->totals.character.coldResist;
    else if(const auto *m=ports_.monsters.find(target.id)) cold=m->rule.resistances[2]>=m->rule.resistances[4];
    MonsterDecisionInput combat{target.id!=id && meleeDistance(monster.position,monster.rule.size,point,size)<=monster.rule.meleeRange && clear,
        controller.charged,distance,monster.rule.difficulty,controller.random,monster.hitOccurrence!=controller.observedHit,false,
        int(monster.life*100/monster.maximumLife),controller.commanded,controller.alerted,
        monster.identity.ownerSpawnKey.empty() || monster.identity.ownerSpawnKey==monster.identity.spawnKey,controller.phase,controller.loop,100,false,clear};
    if(kind==MonsterAiKind::BatDemon) combat.retreatBlocked=area.definition.collision.path(monster.position,monsterRetreatPoint(monster.position,point,combat.lifePercent<monster.rule.ai.params[0]?15:12),true,monster.rule.collision).empty();
    Act2DecisionInput input{combat,tick.tick,controller.skillDeadline,controller.novaAt,controller.firewallAt,monster.nestSpawned,monster.burrowed,bool(corpse),bool(heal),cold};
    input.hasTarget=hasTarget;input.sameRoom=area.definition.activation.room(point)==area.definition.activation.room(monster.position);
    if(kind==MonsterAiKind::GreaterMummy) for(const auto &[key,other]:ports_.monsters.read().actors) {(void)other;if(ports_.monsters.mummySupportTarget(id,key,tick.tick,false,true) || ports_.monsters.mummySupportTarget(id,key,tick.tick,true)) ++input.supportCount;}
    input.canLand=ports_.monsters.canLand(id);input.airborne=monster.airborne;input.attempts=controller.act2Attempts;
    const auto squared=[&](Vec p) {const int x=int(p.x)-int(monster.position.x),y=int(p.y)-int(monster.position.y);return x*x+y*y;};
    input.targetDistanceSquared=squared(point);
    std::optional<Vec> ally;int allyDistance=INT32_MAX;
    for(const auto &[key,other]:ports_.monsters.read().actors) {
        if(key==id || other.area!=monster.area || other.life<=0 || !area.definition.activation.nearby(monster.position,other.position)) continue;
        const int distanceSquared=squared(other.position);
        if(distanceSquared<=121 && other.life*100/other.maximumLife<=monster.rule.ai.params[2]) input.woundedNearby=true;
        if((kind==MonsterAiKind::SandRaider?other.enemyTarget():other.rule.baseClass==monster.rule.baseClass) && distanceSquared<allyDistance) {ally=other.position;allyDistance=distanceSquared;}
    }
    for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==monster.area && p.persistent.player.hp>0 && squared(p.position)<=121 && p.persistent.player.hp*100/p.totals.character.maxLife<=monster.rule.ai.params[2]) input.woundedNearby=true;}
    input.hasAlly=bool(ally);input.allyDistanceSquared=ally?allyDistance:0;
    auto result=decideAct2Monster(monster.rule.ai,input);
    auto &decision=result.action;
    DomainResult<> accepted{DomainStatus::Applied,std::monostate{}};
    if(result.color>=0 || result.overlay>=0) {
        accepted=ports_.monsters.aiColor(id,std::max(0,result.color),result.overlay,tick.tick);
        if(accepted.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    }
    auto random=decision.random;
    if(result.pose>=0) accepted=ports_.monsters.aiPose(id,uint8_t(result.pose),result.holdPose,result.flyMove,tick.tick);
    if(result.flyMove && accepted) {
        Vec goal;
        if(controller.flightGoal && monsterAiDistance(*controller.flightGoal,0,monster.position)>1) goal=*controller.flightGoal;
        else {
            const int radius=decision.phase+8;
            goal={point.x+float(int(limitedRandom(random,uint32_t(2*radius)))-radius),point.y+float(int(limitedRandom(random,uint32_t(2*radius)))-radius)};
            const int minimum=std::clamp(2*radius,12,36);
            while(monsterAiDistance(goal,0,monster.position)<minimum) {goal.x+=goal.x<monster.position.x?-1.f:1.f;goal.y+=goal.y<monster.position.y?-1.f:1.f;}
            controller.flightGoal=goal;
        }
        controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,75,false,8}));
    }
    else if(result.exhaust) accepted=ports_.monsters.damage(id,id,monster.life,tick.tick);
    else if(result.spawnQueen) {
        accepted=ports_.monsters.spawnSkillChildren(id,point,1,false,tick.tick);
        // Fn066 enters its delay stage even when the spawn location is blocked.
        if(accepted.status==DomainStatus::Unavailable) accepted={DomainStatus::Applied,std::monostate{}};
    }
    else if(decision.action==MonsterDecisionAction::Special || decision.action==MonsterDecisionAction::Attack) {
        const auto skill=decision.action==MonsterDecisionAction::Special?monster.rule.skillIds.at(size_t(decision.skillSlot)):0;
        auto victim=target;
        if(monster.rule.ai.kind==MonsterAiKind::GreaterMummy && decision.skillSlot==0 && corpse) victim=*corpse;
        if(monster.rule.ai.kind==MonsterAiKind::GreaterMummy && decision.skillSlot==1 && heal) victim=*heal;
        const auto prepared=monster.rule.skillActions.find(uint16_t(skill));
        const uint8_t mode=skill && prepared!=monster.rule.skillActions.end()?prepared->second.nativeMode:decision.attackMode;
        accepted=ports_.skills.requestCast({id,uint16_t(skill),victim,tick.tick,mode});
    } else if(decision.action==MonsterDecisionAction::Approach) {
        const Vec goal=result.seekAlly?*ally:decision.approachRadius>0?monsterRadiusApproachPoint(monster.position,monster.rule.size,point,decision.approachRadius):point;
        controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},result.seekAlly || decision.approachRadius>0?EntityId{}:target.id,result.seekAlly?0:decision.stopDistance,decision.velocityPercent,decision.running,decision.pathSteps}));
    } else if(decision.action==MonsterDecisionAction::Circle || decision.action==MonsterDecisionAction::Retreat || decision.action==MonsterDecisionAction::Wander) {
        if(decision.action==MonsterDecisionAction::Circle) {
            for(const Vec goal:monsterCirclePoints(monster.position,point,decision.stopDistance,random)) if(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,decision.velocityPercent,false})) {controller.pursuing=true;break;}
        } else {
            const Vec goal=decision.action==MonsterDecisionAction::Retreat?monsterRetreatPoint(monster.position,point,decision.stopDistance):monsterWanderPoint(monster.position,decision.stopDistance,random);
            controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,decision.velocityPercent,decision.running}));
        }
        if(!controller.pursuing && decision.attackWhenMoveFails) {
            const uint8_t mode=kind==MonsterAiKind::Baboon?(int(limitedRandom(random,100))<monster.rule.ai.params[3]?4:5):decision.failedMoveAttackMode;
            accepted=ports_.skills.requestCast({id,0,target,tick.tick,mode});
        }
        if(!controller.pursuing && decision.failedMoveWanderRadius) controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,monsterWanderPoint(monster.position,decision.failedMoveWanderRadius,random)},{},0,decision.velocityPercent,decision.running}));
    }
    if(accepted.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
    controller.random=random;
    if(accepted) {
        controller.phase=decision.phase;controller.loop=decision.loop;controller.charged=decision.charged;controller.skillDeadline=result.deadline;controller.novaAt=result.novaAt;controller.firewallAt=result.firewallAt;controller.act2Attempts=result.attempts;
        if(result.regenerationEighths>=0) ports_.monsters.aiRegeneration(id,result.regenerationEighths);
    }
    if(decision.commandParty) commandParty(monster);
    if(monster.rule.ai.kind==MonsterAiKind::Scarab && accepted && decision.action==MonsterDecisionAction::Special) controller.commanded=false;
    controller.observedHit=monster.hitOccurrence;controller.nextDecision=tick.tick+uint64_t(result.overlay>=0?monster.rule.decisionTicks+1:controller.pursuing && !result.flyMove?1:std::max(1,decision.waitFrames));
    return StepStatus::Complete;
}
}
