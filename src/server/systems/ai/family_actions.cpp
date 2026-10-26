#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/ranged_decision.hpp"
#include "gameplay/monsters/special_decision.hpp"
#include "gameplay/monsters/shaman_decision.hpp"
#include "gameplay/monsters/boss_decision.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::ai {
StepStatus System::fetishShamanAction(EntityId id,std::optional<UnitTarget> target,Vec position,TickContext tick,Controller &controller) {
    const auto &monster=*ports_.monsters.find(id);const auto &area=ports_.areas.at(monster.area);
    controller.pursuing=false;ports_.monsters.stop(id);
    const auto stats=ports_.effects.unitStatValues(id,tick.tick);if(!stats) return StepStatus::Blocked;
    const auto ranks=nativeMonsterSkillRanks(monster.rule.baseSkillRanks,*stats,*monster.rule.stats,*monster.rule.formulas);
    const auto skill=monster.rule.skillIds[0];const auto learned=ranks.find(skill);
    const int rank=learned==ranks.end()?1:std::max(learned->second,1);
    const bool inferno=ports_.effects.unitStates(id,tick.tick).contains(monster.rule.formulas->infernoState);
    MonsterDecisionInput input;input.random=controller.random;input.hasTarget=bool(target);
    input.distance=target?monsterAiDistance(position,target->type==0?0:ports_.monsters.find(target->id)->rule.size,monster.position):INT32_MAX;
    std::optional<UnitTarget> corpse;int squared=INT32_MAX;
    if(!target || input.distance>=rank || inferno) if(const auto *room=area.definition.activation.room(monster.position)) {
        const auto index=size_t(room-area.definition.activation.rooms().data());
        for(const auto near:area.definition.roomNear.at(index)) for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
            if(!ports_.monsters.resurrectionTarget(id,candidate,tick.tick)) continue;
            const auto &dead=*ports_.monsters.find(candidate);
            const int x=int(std::floor(monster.position.x))-int(std::floor(dead.position.x));
            const int y=int(std::floor(monster.position.y))-int(std::floor(dead.position.y));
            const int distance=x*x+y*y;if(distance<squared) {squared=distance;corpse=UnitTarget{candidate,0,1};}
        }
    }
    const auto choice=decideFetishShaman(monster.rule.ai,input,rank,inferno,bool(corpse),
        corpse && ports_.monsters.resurrectionPlacement(corpse->id),squared);
    auto &random=controller.random;random=choice.action.random;
    const int turn=choice.action.action==MonsterDecisionAction::Circle?((rollRandom(random)&255)<128?1:-1):0;
    const auto at=choice.action.action==MonsterDecisionAction::Wander && corpse?
        std::optional{monsterWanderPoint(ports_.monsters.find(corpse->id)->position,choice.action.stopDistance,random)}:std::nullopt;
    if(choice.clearInferno) {
        const auto result=perform(controller,[this,id,state=monster.rule.formulas->infernoState,tick]{return ports_.monsters.toggleAiState(id,state,false,tick.tick);});
        if(isDeferred(result.status)) return StepStatus::Blocked;
    }
    const auto victim=choice.corpseTarget?corpse:target;
    if(choice.command && victim) {
        if(const auto *owner=ports_.monsters.find(ports_.monsters.minionOwner(id))) for(const auto minion:owner->nativeMinions)
            if(const auto *unit=ports_.monsters.find(minion)) {
                auto [entry,fresh]=state_.controllers.try_emplace(minion);
                if(fresh) {entry->second.actor=minion;entry->second.admissionRevision=unit->admissionRevision;}
                entry->second.commands.push_front({choice.command,*victim});
            }
    }
    const auto &decision=choice.action;
    DomainResult<> result{DomainStatus::Applied,std::monostate{}};
    if(decision.action==MonsterDecisionAction::Special && victim)
        result=requestAction(controller,{id,monster.rule.skillIds[size_t(decision.skillSlot)],*victim,tick.tick,decision.attackMode});
    else if(decision.action==MonsterDecisionAction::Circle) {
        result=perform(controller,[this,id,position,target,turn,region=monster.area,generation=area.generation] {
            return ports_.monsters.requestMove({id,{region,generation,position},target?target->id:EntityId{},0,75,false,1,turn,4});
        });controller.pursuing=bool(result);
    } else if(at) {
        result=perform(controller,[this,id,point=*at,region=monster.area,generation=area.generation] {
            return ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
        });controller.pursuing=bool(result);
    }
    controller.nextDecision=tick.tick+uint64_t(controller.pursuing?1:!result?10:std::max(1,decision.waitFrames));
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::familyAction(EntityId id, UnitTarget target, Vec position, int size, TickContext tick, Controller &controller,int continuation) {
    const auto &monster = *ports_.monsters.find(id);
    const auto &area = ports_.areas.at(monster.area);
    const auto distance = meleeDistance(monster.position, monster.rule.size, position, size);
    const bool clear = area.definition.collision.missileSegment(monster.position,position,{4,1});
    if (monster.rule.ai.kind == MonsterAiKind::Fallen) {
        const monsters::Actor *corpse=nullptr;
        if(const auto *room=area.definition.activation.room(monster.position)) {
            const auto index=size_t(room-area.definition.activation.rooms().data());
            for(const auto near:area.definition.roomNear.at(index)) {
                for(const auto dead:ports_.areas.roomLastDead(near)) if(const auto *unit=ports_.monsters.find(dead)) {
                    const auto point=unit->position+ports_.areas.at(unit->area).definition.origin-area.definition.origin;
                    if(!unit->alive() && tick.tick<unit->busyUntil && monsterAiDistance(point,0,monster.position)<15) {corpse=unit;break;}
                }
                if(corpse) break;
            }
        }
        if(corpse) {
            const Vec goal=monsterRetreatPoint(monster.position,position,12);
            controller.commanded=false;if(!controller.commands.empty()) controller.commands.pop_front();controller.alerted=true;
            controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,125,false,1,0,0,13,12}));
            if(controller.pursuing) {
                // The original sound choice consumes the unit random stream.
                limitedRandom(controller.random,20);controller.nextDecision=tick.tick+1;return StepStatus::Complete;
            }
        }
        if(monster.moving || !monster.route.empty() || (monster.aiPose && monster.aiPose!=1)) {
            ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+10;return StepStatus::Complete;
        }
    }
    if(monster.rule.ai.kind==MonsterAiKind::Fallen && !controller.commands.empty()) {
        controller.commanded=controller.commands.front().code==1;
        if(!controller.commanded) {controller.commands.pop_front();ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;return StepStatus::Complete;}
    }
    controller.pursuing = false; ports_.monsters.stop(id);
    int targetLife = target.id?100:0;
    if (const auto *pet = ports_.monsters.find(target.id)) targetLife = playerLifePercentage(pet->life,pet->maximumLife);
    else for (const auto &[key, player] : ports_.players.all()) { (void)key; if (player.actor == target.id) targetLife = playerLifePercentage(player.persistent.player.hp.raw(),player.totals.character.maxLifeFixed.raw()); }
    MonsterDecisionInput input
        {distance <= monster.rule.meleeRange && clear, controller.charged,
         monsterAiDistance(position, target.type==0?0:size, monster.position), monster.rule.difficulty, controller.random,
         monster.hitOccurrence != controller.observedHit, int(monster.area) == 17,
         int(monster.life * 100 / monster.maximumLife), controller.commanded, controller.alerted,
         ports_.monsters.minionOwner(id) == id, controller.phase, controller.loop, targetLife, monster.rule.web && ports_.effects.unitStates(id,tick.tick).contains(monster.rule.web->aura.id), clear};
    input.trapAxisAligned=std::abs(int(std::floor(position.x))-int(std::floor(monster.position.x)))<6 || std::abs(int(std::floor(position.y))-int(std::floor(monster.position.y)))<6;
    for(size_t slot=0;slot<monster.rule.skillIds.size();++slot) if(monster.rule.skillIds[slot] && monster.rule.skillActions.contains(monster.rule.skillIds[slot])) input.skillMask|=uint8_t(1u<<slot);
    input.upgradeCooldown=controller.upgradeCooldown;
    input.continuation=continuation;
    input.hasTarget=bool(target.id);
    if(monster.rule.ai.kind==MonsterAiKind::Smith) {
        const int maximum=int(monster.maximumLife>>8);
        input.lifePercent=maximum?int((monster.life>>8)*100/maximum):0;
    }
    const auto shootable=(monster.rule.ai.kind==MonsterAiKind::SkeletonBow || monster.rule.ai.kind==MonsterAiKind::SkeletonMage ||
        monster.rule.ai.kind==MonsterAiKind::Bighead || monster.rule.ai.kind==MonsterAiKind::FallenShaman ||
        monster.rule.ai.kind==MonsterAiKind::FetishBlowgun || monster.rule.ai.kind==MonsterAiKind::Vampire || monster.rule.ai.kind==MonsterAiKind::CorruptArcher)?shootableTarget(monster,tick):std::nullopt;
    input.random=controller.random; // A Confuse missile-target query is part of this same AI loan.
    input.canShoot=bool(shootable);input.shootDistance=shootable?shootable->distance:INT32_MAX;
    std::optional<UnitTarget> corpse;
    if(monster.rule.ai.kind==MonsterAiKind::FallenShaman) {
        const auto inspect=[&](EntityId candidate) {if(ports_.monsters.resurrectionTarget(id,candidate,tick.tick)) corpse=UnitTarget{candidate,0,1};};
        if(nativeUniqueMonster(monster.identity.rank) && monster.identity.rank!=MonsterRank::Champion) {
            if(const auto *room=area.definition.activation.room(monster.position))
                for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
                    for(const auto candidate:ports_.areas.roomUnitOrder(near)) inspect(candidate);
        } else if(const auto *owner=ports_.monsters.find(ports_.monsters.minionOwner(id)))
            for(const auto candidate:owner->nativeMinions) inspect(candidate);
    }
    std::optional<MonsterBossDecision> boss;
    uint64_t actionWait=0;
    int firewallPhase=controller.phase;
    if(monster.rule.ai.kind==MonsterAiKind::Countess && firewallPhase>=int(monster.skillPositions.size()) && tick.tick-controller.firewallCycle>700) firewallPhase=0;
    if(hasBossDecision(monster.rule.ai.kind)) {
        const auto *homeRoom=area.definition.activation.room(monster.home);
        const auto *monsterRoom=area.definition.activation.room(monster.position);
        const auto *targetRoom=area.definition.activation.room(position);
        boss=decideBossMonster(monster.rule.ai,{input,monsterAiDistance(monster.home,0,monster.position),monsterAiDistance(monster.home,0,position),controller.summonChance,homeRoom==monsterRoom,firewallPhase<int(monster.skillPositions.size()),homeRoom==targetRoom});
    }
    auto decision = boss?std::optional{boss->action}:monster.rule.ai.kind==MonsterAiKind::FallenShaman?std::optional{decideFallenShaman(monster.rule.ai,input,bool(corpse),corpse && ports_.monsters.resurrectionPlacement(corpse->id))}:
        hasRangedDecision(monster.rule.ai.kind)?decideRangedMonster(monster.rule.ai,input):hasSpecialDecision(monster.rule.ai.kind)?decideSpecialMonster(monster.rule.ai,input):decideMeleeMonster(monster.rule.ai,input);
    if (!decision) return StepStatus::NotImplemented;
    controller.random=decision->random;
    if(decision->commandParty) commandParty(monster);
    const auto actionTarget=decision->useShootableTarget && shootable?shootable->unit:target;
    const auto actionPosition=decision->useShootableTarget && shootable?shootable->position:position;
    if (decision->action == MonsterDecisionAction::Shout) {
        const auto result = requestAction(controller,{id, 0, target, tick.tick, 9});
        if (isDeferred(result.status)) { controller.nextDecision = tick.tick + 1; return StepStatus::Blocked; }
        if(decision->commandParty) controller.commanded=true;
    } else if (decision->action == MonsterDecisionAction::Attack || decision->action == MonsterDecisionAction::Special) {
        const auto skill=decision->action==MonsterDecisionAction::Special?monster.rule.skillIds.at(size_t(decision->skillSlot)):uint16_t(0);
        const auto actualTarget=monster.rule.ai.kind==MonsterAiKind::FallenShaman && decision->skillSlot==0?*corpse:actionTarget;
                std::optional<Vec> point;
        if(boss && monster.rule.ai.kind==MonsterAiKind::Countess && decision->skillSlot==0) point=monster.skillPositions.at(size_t(firewallPhase));
        if(boss && monster.rule.ai.kind==MonsterAiKind::BloodRaven && decision->skillSlot==0) point=Vec{std::floor(position.x)+.5f,std::floor(position.y)+.5f}+boss->summonOffset;
        const auto mode=decision->action==MonsterDecisionAction::Special?monster.rule.skillActions.at(skill).nativeMode:decision->attackMode;
        const auto result = requestAction(controller,{id,skill,actualTarget,tick.tick,mode,point});
        if (isDeferred(result.status)) {
            controller.nextDecision = tick.tick + 1;
            return StepStatus::Blocked; // Preserve the chosen roll and charge through output pressure.
        }
        if (!result) {
            if(monster.rule.ai.kind==MonsterAiKind::ThornHulk || monster.rule.ai.kind==MonsterAiKind::Fetish || monster.rule.ai.kind==MonsterAiKind::Vampire || monster.rule.ai.kind==MonsterAiKind::BloodHawk || monster.rule.ai.kind==MonsterAiKind::CorruptLancer) decision->waitFrames=10;
            else {controller.random=decision->random;controller.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        if(result && monster.rule.ai.kind==MonsterAiKind::ThornHulk && decision->skillSlot==0) {
            // Fn027 schedules its next think at ENDANIM + aip5, rather
            // than the ordinary neutral-mode AI delay.
            actionWait=ports_.monsters.find(id)->busyUntil+uint64_t(monster.rule.ai.params[4]);
        }
        if(boss && monster.rule.ai.kind==MonsterAiKind::Countess && decision->skillSlot==0) {firewallPhase++;controller.firewallCycle=tick.tick;}
    } else if (decision->action == MonsterDecisionAction::Circle || decision->action == MonsterDecisionAction::Retreat || decision->action == MonsterDecisionAction::Wander) {
        auto random = decision->random;
        auto freeMove = [&](Vec goal) {
            const bool fetish=monster.rule.ai.kind==MonsterAiKind::Fetish || monster.rule.ai.kind==MonsterAiKind::FetishBlowgun || monster.rule.ai.kind==MonsterAiKind::Vampire || monster.rule.ai.kind==MonsterAiKind::BloodHawk || monster.rule.ai.kind==MonsterAiKind::Zombie;
            const uint8_t path=13; // Default WALK resolves 101 to TOWARD_FINISH.
            const auto pathDistance=uint8_t(std::clamp(std::max(decision->pathDistance,fetish && decision->action==MonsterDecisionAction::Retreat?decision->stopDistance:5),1,255));
            return ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,decision->velocityPercent,decision->running,std::max(decision->pathSteps,fetish?1:0),0,0,path,pathDistance});
        };
        if (decision->action == MonsterDecisionAction::Circle) {
            const int turn=(rollRandom(random)&255)<128?1:-1;
            controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,actionPosition},actionTarget.id,0,decision->velocityPercent,decision->running,0,turn,decision->stopDistance}));
        } else {
            const Vec goal = decision->action == MonsterDecisionAction::Retreat ? monsterRetreatPoint(monster.position, actionPosition, decision->stopDistance) : monsterWanderPoint(decision->wanderAroundTarget?actionPosition:monster.position, decision->stopDistance, random);
            controller.pursuing = bool(freeMove(goal));
        }
        if (!controller.pursuing && monster.rule.ai.kind == MonsterAiKind::Fetish && input.phase==2 && decision->action == MonsterDecisionAction::Retreat) {
            controller.random = random; controller.observedHit = monster.hitOccurrence;
            controller.phase = controller.loop = 0; controller.nextDecision = tick.tick + 10; return StepStatus::Complete;
        }
        if(!controller.pursuing && decision->bloodRavenCloseWhenMoveFails) {
            auto close=input;close.random=random;close.phase=decision->phase;close.loop=decision->loop;
            const auto fallback=decideBloodRavenClose(close);random=fallback.random;
            if(fallback.action==MonsterDecisionAction::Retreat) {
                const Vec goal=monsterRetreatPoint(monster.position,position,fallback.stopDistance);
                controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,goal},{},0,fallback.velocityPercent,fallback.running}));
            }
            if(!controller.pursuing) {
                controller.random=random;
                const auto result=requestAction(controller,{id,0,target,tick.tick,4});
                if(isDeferred(result.status)) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
            }
        }
        if(!controller.pursuing && decision->failedEscapeContinuation) {
            controller.random=random;controller.phase=decision->phase;controller.loop=decision->loop;
            controller.upgradeCooldown=decision->upgradeCooldown;
            return familyAction(id,target,position,size,tick,controller,decision->failedEscapeContinuation);
        }
        if (!controller.pursuing && decision->attackWhenMoveFails) {
            controller.random=random;
            const bool blowgun=monster.rule.ai.kind==MonsterAiKind::FetishBlowgun;
            const auto fallback=blowgun && shootable?shootable->unit:target;
            const auto result = blowgun && !shootable?DomainResult<>{DomainStatus::Unavailable,{}}:
                requestAction(controller,{id,0,fallback,tick.tick,decision->failedMoveAttackMode});
            if (isDeferred(result.status)) {controller.nextDecision = tick.tick + 1; return StepStatus::Blocked; }
            if(blowgun && !result) decision->waitFrames=10;
        }
        if(!controller.pursuing && decision->failedMoveWanderRadius) controller.pursuing=bool(freeMove(monsterWanderPoint(monster.position,decision->failedMoveWanderRadius,random)));
        controller.random=random;controller.observedHit=monster.hitOccurrence;
        controller.charged=decision->charged;controller.alerted=decision->alerted;
        if(boss) controller.summonChance=boss->summonChance;
        controller.phase=decision->phase;controller.loop=decision->loop;
        controller.upgradeCooldown=decision->upgradeCooldown;
        if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::FetishBlowgun &&
            (decision->action==MonsterDecisionAction::Retreat || (decision->action==MonsterDecisionAction::Circle && decision->phase==2))) {
            controller.phase=controller.loop=0;
        }
        const bool stalled=!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::FetishBlowgun &&
            decision->action==MonsterDecisionAction::Circle && (decision->stopDistance==6 || decision->phase==2);
        controller.nextDecision = tick.tick + uint64_t(controller.pursuing ? 1 : stalled?10:std::max(1, decision->waitFrames));
        return StepStatus::Complete;
    } else if (decision->action == MonsterDecisionAction::Approach) {
        const auto stop=decision->pathSteps>0?decision->pathSteps-1:decision->stopDistance;
        const auto actionSize=decision->useShootableTarget && shootable?shootable->size:size;
        if (decision->approachRadius || !(meleeDistance(monster.position,monster.rule.size,actionPosition,actionSize)<=stop &&
              area.definition.collision.missileSegment(monster.position,actionPosition,{4,1}))) {
            if (decision->approachRadius) {
                const Vec goal = monsterRadiusApproachPoint(monster.position, monster.rule.size, position, decision->approachRadius,decision->targetDistance);
                controller.pursuing = bool(ports_.monsters.requestMove({id, {monster.area, area.generation, goal}, {}, 0, decision->velocityPercent, decision->running,decision->pathSteps}));
                if(!controller.pursuing && (monster.rule.ai.kind==MonsterAiKind::Wraith || monster.rule.ai.kind==MonsterAiKind::SkeletonBow)) decision->waitFrames=10;
            } else if(boss && boss->returnHome) controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,monster.home},{},0,decision->velocityPercent,decision->running}));
            else controller.pursuing = bool(ports_.monsters.requestMove({id,{monster.area,area.generation,actionPosition},actionTarget.id,decision->stopDistance,decision->velocityPercent,decision->running,decision->pathSteps,0,0,13,uint8_t(decision->pathDistance)}));
            if(!controller.pursuing && ((monster.rule.ai.kind==MonsterAiKind::FlyingScimitar && !decision->approachRadius) || monster.rule.ai.kind==MonsterAiKind::Smith || monster.rule.ai.kind==MonsterAiKind::DoomKnight || monster.rule.ai.kind==MonsterAiKind::ThornHulk || monster.rule.ai.kind==MonsterAiKind::Vampire || monster.rule.ai.kind==MonsterAiKind::Skeleton || monster.rule.ai.kind==MonsterAiKind::Goatman || monster.rule.ai.kind==MonsterAiKind::Brute || (monster.rule.ai.kind==MonsterAiKind::Bighead && input.lifePercent>=monster.rule.ai.params[0]) ||
                (monster.rule.ai.kind==MonsterAiKind::Fallen && !input.commanded && !input.retaliating) ||
                (monster.rule.ai.kind==MonsterAiKind::CorruptRogue && !decision->running) ||
                (monster.rule.ai.kind==MonsterAiKind::Fetish && !input.commanded))) {
                // WalkToTargetUnitWithFlags(7): on failure, roll the
                // 70/30 neutral pause versus WalkCloseToUnit fallback.
                if(rollRandom(decision->random)%100>=70) decision->waitFrames=10;
                else {
                    const auto point=monsterWanderPoint(monster.position,4,decision->random);
                    controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,point},{},0,75,false,1}));
                }
            }
            if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::Fallen) {controller.commanded=false;if(!controller.commands.empty()) controller.commands.pop_front();decision->waitFrames=10;}
            if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::Zombie) decision->waitFrames=10;
            if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::CorruptRogue && decision->running) decision->waitFrames=10;
            if(!controller.pursuing && (monster.rule.ai.kind==MonsterAiKind::CorruptLancer || monster.rule.ai.kind==MonsterAiKind::SkeletonMage)) decision->waitFrames=10;
            if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::Bighead && input.lifePercent<monster.rule.ai.params[0]) decision->waitFrames=10;
            controller.nextPath = tick.tick + 18;
        }
    }
    controller.random = decision->random; controller.charged = decision->charged;
    controller.observedHit = monster.hitOccurrence;
    controller.alerted = decision->alerted;
    controller.phase = monster.rule.ai.kind==MonsterAiKind::Countess?firewallPhase:decision->phase; controller.loop = decision->loop;controller.upgradeCooldown=decision->upgradeCooldown;
    if(boss) controller.summonChance=boss->summonChance;
    if(monster.rule.ai.kind==MonsterAiKind::Fetish && input.commanded) controller.commanded=false;
    // 357730 posts Idle(2 * aidel(H)) before delegating Fn007. A delegate's
    // explicit Idle replaces that event; a mode/path change retains it.
    if(monster.rule.stats && monster.rule.stats->pd2 && monster.rule.ai.kind==MonsterAiKind::Brute &&
       monster.rule.nativeClass==1103 && !decision->waitFrames)
        actionWait=std::max(actionWait,tick.tick+uint64_t(2*monster.rule.ai.pd2FixedIdle));
    controller.nextDecision = std::max(actionWait,tick.tick + uint64_t(std::max(1, decision->waitFrames)));
    return StepStatus::Complete;
}
void System::commandParty(const monsters::Actor &leader) {
    const auto deliver=[&](const monsters::Actor &minion,bool command) {
        auto [entry,fresh]=state_.controllers.try_emplace(minion.id);auto &controller=entry->second;
        if(fresh) {controller.random=minion.combatRandom;controller.actor=minion.id;controller.admissionRevision=minion.admissionRevision;}
        if(command) controller.commands.push_front({1,{}});
        controller.commanded=true;
    };
    if(leader.rule.ai.kind==MonsterAiKind::Fallen || leader.rule.ai.kind==MonsterAiKind::FallenShaman || leader.rule.ai.kind==MonsterAiKind::Scarab) {
        if(const auto *owner=ports_.monsters.find(ports_.monsters.minionOwner(leader.id)))
            for(const auto id:owner->nativeMinions) if(const auto *minion=ports_.monsters.find(id)) deliver(*minion,true);
        if(leader.rule.ai.kind==MonsterAiKind::Fallen || leader.rule.ai.kind==MonsterAiKind::Scarab) deliver(leader,true);
        return;
    }
    for(const auto &[id,minion]:ports_.monsters.read().actors)
        if(minion.life>0 && minion.area==leader.area && ports_.monsters.minionOwner(id)==leader.id) deliver(minion,false);
}
}
