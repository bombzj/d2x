#include "system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/pd2_phasing.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/facts/combat.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "world/native_target_path.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/combat/participants.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "system_detail.hpp"
namespace d2x::server::monsters {
int movementPercent(const Actor &actor,uint64_t) {
    const int64_t value=int64_t(actor.velocityPercent)+actor.effectVelocity+actor.hirelingMovementPercent+
        (actor.necroPet?actor.necroPet->velocityPercent:0)+(actor.rule.enchantment?actor.rule.enchantment->velocityPercent:0);
    return int(std::clamp<int64_t>(value,25,INT16_MAX));
}
std::optional<std::pair<Vec,int>> System::targetPosition(EntityId id,RegionId area) const {
    for(const auto &[key,p]:ports_.players.all()) {
        (void)key;if(p.actor==id && p.entered && p.area==area && p.persistent.player.hp>0) return std::pair{p.position,2};
    }
    const auto *m=find(id);
    if(m && m->area==area && m->alive() && m->damageable()) return std::pair{m->position,m->rule.size};
    return {};
}
std::optional<std::pair<Vec,int>> System::nativeTargetPosition(EntityId id,RegionId region) const {
    const auto *area=ports_.areas.find(region);if(!area) return {};
    // SUNIT_GetTargetUnit is an identity lookup, including a real corpse;
    // harm/life qualification belongs to the receiving program or contact.
    for(const auto &[key,player]:ports_.players.all()) {
        (void)key;if(player.actor!=id || !player.entered) continue;
        const auto *target=ports_.areas.find(player.area);
        if(target && target->definition.act==area->definition.act)
            return std::pair{player.position+target->definition.origin-area->definition.origin,2};
    }
    if(const auto *unit=find(id);unit && !unit->roomless) {
        const auto *target=ports_.areas.find(unit->area);
        if(target && target->definition.act==area->definition.act)
            return std::pair{unit->position+target->definition.origin-area->definition.origin,unit->rule.size};
    }
    if(const auto found=ports_.objects.read().objects.find(id);found!=ports_.objects.read().objects.end()) {
        const auto &object=found->second;const auto *target=ports_.areas.find(object.area);
        if(target && target->definition.act==area->definition.act)
            return std::pair{object.position+target->definition.origin-area->definition.origin,object.rule.width};
    }
    return {};
}
std::optional<MotionTarget> System::nativeTargetIdentity(EntityId id,RegionId region) const {
    if(!nativeTargetPosition(id,region)) return {};
    return MotionTarget{id,uint8_t(ports_.players.findActor(id)?0:find(id)?1:2)};
}
bool System::recacheMovementRoom(Actor &actor,Vec previous) {
    constexpr Vec center{.5f,.5f};
    const auto &source=ports_.areas.at(actor.area);
    if(source.definition.activation.room(actor.position+center)) return true;
    const auto global=actor.position+source.definition.origin+center;
    const auto room=ports_.areas.roomAtPosition(actor.area,previous+center,
        int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return false;
    const auto &destination=ports_.areas.at(room->area);
    const auto offset=source.definition.origin-destination.definition.origin;
    // PATH_RecacheRoomAtCoordIfNeeded moves room ownership without restarting
    // the path or treating a continuous level boundary as a pet teleport.
    actor.area=destination.definition.id;
    actor.position=actor.pathStep.translate(actor.position+center,offset)-center;
    actor.movementGoal=actor.movementGoal+offset;
    for(auto &point:actor.route) point=point+offset;
    return true;
}
DomainResult<> System::requestMove(const MoveRequest &request) {
    const auto it = state_.actors.find(request.actor);
    if (it == state_.actors.end() || !it->second.alive()) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto &area = ports_.areas.at(actor.area);
    if (request.destination.area != actor.area || request.destination.generation != area.generation)
        return {DomainStatus::Stale, {}};
    // Native paths retain a unit target independently of combat eligibility:
    // Fenris and resource vines must be able to approach a real corpse.
    if (actor.rule.nativeVelocity <= 0 || (request.target && !nativePathTarget(ports_.players,*this,request.target,actor.area)) || request.stopDistance<0 || request.stopDistance>255 ||
        request.velocityPercent<25 || request.velocityPercent>INT16_MAX) return {DomainStatus::InvalidRequest,{}};
    auto collision=(actor.airborne || actor.submerged)?MovementCollisionRule{0,1}:actor.rule.collision;
    if(collision.mask && actor.rule.stats && actor.rule.stats->pd2) {
        const auto states=ports_.effects.unitStates(actor.id,ports_.frameEvents.currentFrame());
        if(std::any_of(states.begin(),states.end(),pd2PhasingState)) collision.mask=pd2PhasingMovementMask(true);
    }
    uint8_t pathType=request.circleTurn?1:request.pathType;
    // MonsterMode's default obstacle-search distance is 5; pet tactics supply
    // their own 40/100 bounds. Circling is
    // published as AStar to its computed last point by SCmd, not as pursuit.
    if(request.circleTurn && (request.circleDistance<1 || request.circleDistance>UINT8_MAX))
        return {DomainStatus::InvalidRequest,{}};
    // AiTactics::sub_6FCD0E80 passes its circle radius through velocity
    // parameters into PATH_SetMaxDistance; SCmd publishes that same value.
    const uint8_t pathDistance=request.circleTurn?uint8_t(request.circleDistance):request.pathDistance;
    if((pathType!=1 && pathType!=2 && pathType!=3 && pathType!=5 && pathType!=6 && pathType!=7 && pathType!=12 && pathType!=13 && pathType!=15) || !pathDistance)
        return {DomainStatus::InvalidRequest,{}};
    const Vec center{.5f,.5f};
    auto pathGrid=ports_.spatial.navigationGrid(actor.area,actor.id,
        !request.circleTurn && (pathType==1 || pathType==7 || pathType==15)?request.target:EntityId{});
    // A retained WallFollow path still needs its unit-target search bound.
    auto route = request.circleTurn?pathGrid.nativeMonsterCirclePath(actor.position+center,request.destination.position+center,request.circleDistance,request.circleTurn,collision):
        pathGrid.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision,bool(request.target));
    if(route.empty() && !request.circleTurn && (pathType==2 || pathType==7 || pathType==13)) {
        // MonsterMode retries failed Toward paths with WallFollow. The
        // solver raises its search bound to 40 for a live unit target.
        pathType=15;
        pathGrid=ports_.spatial.navigationGrid(actor.area,actor.id,request.target);
        route=pathGrid.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision,bool(request.target));
    }
    for(auto &point:route) point=point-center;
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    if(request.pathSteps<0 || request.pathSteps>255) return {DomainStatus::InvalidRequest,{}};
    // PATH_SetStepNum stores steps - 1 as a unit-distance arrival bound.
    // It never limits the number of route points (D2Common PathMisc).
    const auto goal=request.circleTurn?route.back():request.destination.position;
    recordNativeMode(actor,request.running?15:2);
    actor.route = std::move(route); actor.movementTarget = request.circleTurn?EntityId{}:request.target;
    actor.pathSteps=uint8_t(request.pathSteps);
    actor.pathType=pathType;actor.pathDistance=pathDistance;
    // MonsterMode initializes PATH.nDistance independently of max distance.
    actor.pathRemaining=20;actor.pathPointsConsumed=0;
    actor.movementGoal = goal;
    actor.stopDistance = request.pathSteps>0?request.pathSteps-1:request.stopDistance;
    actor.velocityPercent = request.velocityPercent; actor.running = request.running; actor.moving = true;
    if(!actor.airborne) {actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;}
    ++actor.revision;
    actor.movementRevision=actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stopForNpcClick(EntityId id, uint64_t tick, std::optional<Vec> observed) {
    auto &unit = state_.actors.at(id);
    stop(id);
    if (observed) unit.npcClickPosition = observed;
    unit.npcClickDecisions = 40;
    unit.aiThinkAt = tick + 1;
    ++unit.aiThinkRevision;
    scheduleAi(id, unit.aiThinkAt);
}
void System::advanceNpcClick(EntityId id) {
    auto &remaining = state_.actors.at(id).npcClickDecisions;
    if (remaining > 0) --remaining;
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        // Step::PATH_ResetMovement clears points and velocity, not pTargetUnit.
        if (!actor.route.empty() || actor.moving || actor.running) {
            actor.position=actor.pathStep.stop(actor.position+Vec{.5f,.5f})-Vec{.5f,.5f};
            actor.route.clear(); actor.moving = false; actor.running = false; ++actor.revision;
            if(ports_.areas.hasRoomUnit(id)) ports_.areas.moveRoomUnit(id,actor.area,actor.position);
        }
    }
}
DomainResult<> System::beginAttack(EntityId id, uint64_t until, bool returnToNeutral, EntityId target,std::optional<NativeUnitStats> modeStats,uint8_t mode) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || !it->second.alive()) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    if(actor.nativeActionRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    recordNativeMode(actor,mode);
    ++actor.nativeActionRevision;actor.nativeActionInterruption=actor.interruption;actor.nativeActionUntil=until;
    if(modeStats) actor.nativeStats=std::move(modeStats);
    actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;
    actor.neutralAt=returnToNeutral?std::optional{until}:std::nullopt;
    // MonsterMode's mode change installs the action target on the same path.
    // BL must retain that identity after an attacking actor stops walking.
    actor.busyUntil = until; actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = target; ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::extendAttack(EntityId id,uint64_t until) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    actor.nativeActionUntil=std::max(actor.nativeActionUntil,until);
    actor.busyUntil=std::max(actor.busyUntil,until);
    if(actor.neutralAt) actor.neutralAt=std::max(*actor.neutralAt,until);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
uint64_t System::resumeAi(EntityId id,uint64_t tick,uint64_t scheduled) {
    auto found=state_.actors.find(id);
    if(found==state_.actors.end()) return scheduled;
    auto &actor=found->second;
    if(!actor.alive()) {actor.neutralAt.reset();return scheduled;}
    if(!actor.neutralAt || tick<*actor.neutralAt || frozen(id,tick)) return scheduled;
    const auto ended=std::max(*actor.neutralAt,actor.frozenUntil);
    recordNativeMode(actor,1);
    actor.neutralAt.reset();
    // MonsterMode::sub_6FC64310 preserves an already scheduled future AI
    // event. Default aidel starts at ENDANIM, not at attack start, and is
    // independent of busyUntil so the neutral pause cannot extend animation.
    if(scheduled<=ended) {
        const uint64_t delay=actor.stunnedUntil>ended?45:uint64_t(actor.rule.neutralDelayTicks>0?actor.rule.neutralDelayTicks:15);
        scheduled=ended+std::min(delay,UINT64_MAX-ended);
    }
    return scheduled;
}
void System::finishAction(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return;
    auto &actor=found->second;
    // SplEndGeneric's immediate AI branches retain the completed mode.
    // beginAttack already distinguishes them by omitting neutralAt. In
    // particular, resetting a Demon Pillar's Emerge to NU makes its native
    // AI choose Emerge again forever instead of proceeding to its attack.
    if(actor.neutralAt) recordNativeMode(actor,1);
    actor.nativeActionUntil=actor.busyUntil=tick;
    if(actor.skillMotion && (actor.skillMotion->leaperRecovery || actor.skillMotion->eventDriven)) {actor.skillMotion.reset();actor.moving=false;}
    if(actor.neutralAt) actor.neutralAt=tick;
    ++actor.revision;
    const FrameEventOwner owner{FrameEventUnit::Monster,id};
    const auto wake=ports_.frameEvents.nextDue(owner,FrameEventType::AiThink);
    auto next=wake.value_or(tick);
    if(wake) for(const auto &event:ports_.frameEvents.events(owner))
        if(event.type==FrameEventType::AiThink && !event.handler && !event.continuous && event.due==*wake &&
           ports_.frameEvents.find(event.handle)) {
            // Raw native timers have no payload; scheduleAi retains the
            // decision deadline separately from a deferred physical wake.
            next=event.payload?event.payload:event.due;break;
        }
    scheduleAi(id,resumeAi(id,tick,next));
}

StepStatus System::advanceMode(EntityId only,TickContext tick) {
    bool blocked = false;
    for (auto entry=state_.actors.find(only);entry!=state_.actors.end() && entry->first==only;++entry) {
        auto &[id,actor]=*entry;
        if(actor.modeFrame==tick.tick) continue;
        if(actor.delayedDeathSource && !ports_.effects.unitStates(id,tick.tick).contains(actor.rule.uninterruptableState)) {
            const auto result=damage(id,actor.killer,0,tick.tick,0,false,0,{},true,0,{},0,false,0,0,true,{},false,{*actor.delayedDeathSource});
            if(!result) {blocked=true;continue;}
        }
        if(actor.alive() && actor.nativeAi() && !actor.skillMotion && actor.knockedUntil<=tick.tick && actor.stunnedUntil<=tick.tick &&
           tick.tick>=actor.busyUntil && !frozen(id,tick.tick) && !actor.route.empty() && actor.pathStep.changedCell() &&
           ports_.areas.loaded(actor.area,actor.position+Vec{.5f,.5f})) {
            const auto result=ports_.effects.movementSkills(id,tick.tick,actor.pathStep.direction(),actor.pathStep.nativePosition());
            if(!result) {blocked=true;continue;}
        }
        const auto previousModeFrame=actor.modeFrame;
        actor.modeFrame=tick.tick;
        if (!ports_.areas.loaded(actor.area, actor.position+Vec{.5f,.5f})) {
            actor.moving = false; actor.route.clear(); actor.movementTarget = {};
            continue;
        }
        // MonsterMode::sub_6FC641D0 / sub_6FC64280 enter DD at ENDANIM.
        // Replication already projects DD after this same prepared duration;
        // native AI and quest callbacks must observe that authoritative mode too.
        if(!actor.alive() && actor.deathMode && actor.nativeMode==0 && tick.tick>=actor.busyUntil) {
            if(actor.revision==UINT64_MAX) throw std::overflow_error("Monster death mode revision exhausted");
            recordNativeMode(actor,12);++actor.revision;
        }
        if(actor.hydra() && actor.alive() && actor.nativeMode==actor.riseMode && actor.neutralAt && tick.tick>=*actor.neutralAt) {
            if(actor.revision==UINT64_MAX) throw std::overflow_error("Hydra birth mode revision exhausted");
            recordNativeMode(actor,1);actor.neutralAt.reset();++actor.revision;
        }
        if(!actor.nativeAiResetTracked) {
            if(actor.curseControl.ai!=CurseAi::None && actor.curseControl.until<=tick.tick) actor.curseControl={};
            if(actor.attractionUntil<=tick.tick) actor.attractionTarget={};
        }
        (void)id; if(actor.nativeAi()) actor.moving = false;
        if(actor.conversion && (!actor.alive() || actor.life<=0 || !actor.owner || !ports_.players.find(*actor.owner) || !ports_.players.find(*actor.owner)->entered)) {
            const auto result=ports_.effects.endConversion(id,tick.tick);
            if(!result) {blocked=true;continue;}
        }
        if(actor.knockbackGoal && actor.alive() && actor.life>0) {
            if(tick.tick<=actor.knockbackStarted) continue;
            const auto previousPosition=actor.position;
            const auto previousStep=actor.pathStep;
            AreaStore::CollisionMove collisionMove{ports_.areas,actor.id,actor.area};
            const auto grid=ports_.spatial.movementGrid(actor.area,collisionMove);
            const Vec center{.5f,.5f};
            const auto step=actor.pathStep.advance(grid,actor.position+center,*actor.knockbackGoal+center,
                1.f,actor.rule.collision);
            actor.position=step.position-center;
            const bool finished=tick.tick>=actor.knockedUntil;
            const bool leaper=actor.rule.knockbackDistance==10;
            if(finished && !leaper && actor.rule.hitRecoveryTicks>0) {
                const HitFact fact{id,1,actor.area,monsterLifeRatio(actor.life,actor.maximumLife),false,actor.position,160,3};
                if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{fact}})) {
                    actor.position=previousPosition;actor.pathStep=previousStep;actor.modeFrame=previousModeFrame;
                    collisionMove.rollback();blocked=true;continue;
                }
            }
            collisionMove.commit();ports_.areas.moveRoomUnit(id,actor.area,actor.position);
            if(step.blocked) actor.knockbackGoal=actor.position;
            ++actor.revision;
            if(tick.tick<actor.knockedUntil) continue;
            actor.knockbackGoal.reset();actor.knockedUntil=0;
            actor.position=actor.pathStep.stop(actor.position+center)-center;
            ports_.areas.moveRoomUnit(id,actor.area,actor.position);
            if(leaper) {
                actor.neutralAt.reset();actor.reactionUntil=0;
                const bool stunned=ports_.effects.unitStates(id,tick.tick).contains(actor.rule.stunState);
                const uint64_t delay=stunned?45:15;
                actor.busyUntil=tick.tick;
                scheduleAi(id,tick.tick+std::min(delay,UINT64_MAX-tick.tick));
            } else if(actor.rule.hitRecoveryTicks>0) {
                recordNativeMode(actor,3);actor.reactionMode=3;
                actor.reactionUntil=tick.tick+std::min<uint64_t>(recoveryTicks(actor,ports_.effects,tick.tick,false),UINT64_MAX-tick.tick);
                actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;
            } else scheduleAi(id,tick.tick+1);
            continue;
        }
        if(actor.holyFreezeUntil && actor.holyFreezeUntil<=tick.tick) actor.holyFreezeUntil=0;
        if(!actor.nativeAi()) continue;
        if(actor.skillMotion && (actor.skillMotion->eventDriven || advanceSkillMotion(actor,tick.tick))) continue;
        if (frozen(id,tick.tick) || actor.stunnedUntil>tick.tick) continue;
        if (!actor.alive() || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const auto finishWalk=[&] {
            // MonsterMode::sub_6FC63680 dispatches AITHINK at WALK/RUN end.
            // Leave the callback pending until the current mode event commits.
            actor.walkEndAiPending=actor.nativeMode==2 || actor.nativeMode==15;
            recordNativeMode(actor,1);
        };
        const auto target=actor.movementTarget ? nativePathTarget(ports_.players,*this,actor.movementTarget,actor.area) : std::optional(std::pair{actor.movementGoal, 0});
        if(!target) {stop(id);finishWalk();continue;}
        auto collision=(actor.airborne || actor.submerged)?MovementCollisionRule{0,1}:actor.rule.collision;
        if(collision.mask && actor.rule.stats && actor.rule.stats->pd2) {
            const auto states=ports_.effects.unitStates(actor.id,tick.tick);
            if(std::any_of(states.begin(),states.end(),pd2PhasingState)) collision.mask=pd2PhasingMovementMask(true);
        }
        auto arrived=[&](Vec position) {
            // Coordinate paths finish by consuming their last precise point,
            // not by an extra floating stopping radius.
            if (!actor.movementTarget) return false;
            return meleeDistance(position,actor.rule.size,target->first,target->second)<=actor.stopDistance;
        };
        if (arrived(actor.position)) { stop(id);finishWalk();continue; }
        const auto identity=actor.movementTarget?nativeTargetIdentity(actor.movementTarget,actor.area):std::nullopt;
        if(identity && nativeUnitTargetMoved(actor.movementGoal,target->first,identity->type)) {
            if(!actor.pathRemaining) {stop(id);finishWalk();continue;}
            // Common D820 checks the old budget, then deducts consumed points.
            actor.pathRemaining=uint8_t(actor.pathRemaining-std::min<unsigned>(actor.pathRemaining,actor.pathPointsConsumed));
            actor.pathPointsConsumed=0;
            actor.movementRevision=++actor.revision;
            auto type=actor.pathType;
            if(type==2 || type==13 || type==15) type=13;
            const Vec center{.5f,.5f};
            auto pathGrid=ports_.spatial.navigationGrid(actor.area,actor.id,
                type==1 || type==7 || type==15?actor.movementTarget:EntityId{});
            auto route=pathGrid.nativeUnitPath(actor.position+center,target->first+center,type,actor.pathDistance,collision,true);
            if(route.empty() && type==13) {
                type=15;
                pathGrid=ports_.spatial.navigationGrid(actor.area,actor.id,actor.movementTarget);
                route=pathGrid.nativeUnitPath(actor.position+center,target->first+center,type,actor.pathDistance,collision,true);
            }
            for(auto &point:route) point=point-center;
            actor.route=std::move(route);actor.pathType=type;actor.movementGoal=target->first;
            if(actor.route.empty()) {
                actor.position=actor.pathStep.stop(actor.position+center)-center;
                ports_.areas.moveRoomUnit(id,actor.area,actor.position);
                actor.moving=false;actor.running=false;finishWalk();++actor.revision;continue;
            }
        }
        AreaStore::CollisionMove collisionMove{ports_.areas,actor.id,actor.area};
        const auto movementGrid=ports_.spatial.movementGrid(actor.area,collisionMove);
        const int speed = movementPercent(actor,tick.tick);
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, speed) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance == 0) { actor.route.pop_front(); ++actor.pathPointsConsumed; continue; }
            constexpr Vec center{.5f,.5f};
            const auto previousStep=actor.pathStep;
            const auto step=actor.pathStep.advance(movementGrid,actor.position+center,actor.route.front()+center,
                remaining,collision);
            const Vec next=step.position-center;
            const auto previous=actor.position;actor.position=next;
            if(!recacheMovementRoom(actor,previous)) {
                actor.position=previous;actor.pathStep=previousStep;
                collisionMove.rollback();actor.route.clear();break;
            }
            ports_.areas.moveRoomUnit(id,actor.area,actor.position);refreshCollision(actor);
            actor.moving=actor.moving || step.travelled>0;++actor.revision;
            if(step.blocked) {actor.route.clear();break;}
            if(!step.reached) break;
            remaining-=step.travelled;actor.route.pop_front();++actor.pathPointsConsumed;
            if(step.travelled>0) break; // D2Common_10226: one nonzero path point per frame.
        }
        collisionMove.commit();
        // D2Common_10226 reports path exhaustion or collision to the WALK/RUN callback.
        if(actor.route.empty()) {
            actor.position=actor.pathStep.stop(actor.position+Vec{.5f,.5f})-Vec{.5f,.5f};
            ports_.areas.moveRoomUnit(id,actor.area,actor.position);
            actor.moving=false;actor.running=false;finishWalk();
        }
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
} // namespace d2x::server::monsters
