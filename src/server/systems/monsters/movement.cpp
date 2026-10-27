#include "system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_phasing.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/spatial/system.hpp"
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
    if(const auto *unit=find(id)) {
        const auto *target=ports_.areas.find(unit->area);
        if(target && target->definition.act==area->definition.act)
            return std::pair{unit->position+target->definition.origin-area->definition.origin,unit->rule.size};
    }
    return {};
}
bool System::recacheHirelingRoom(Actor &actor) {
    if(!actor.hireling) return true;
    constexpr Vec center{.5f,.5f};
    const auto &source=ports_.areas.at(actor.area);
    if(source.definition.activation.room(actor.position+center)) return true;
    Vec navigationOrigin;
    const auto navigation=source.definition.collision.linkedNavigation(navigationOrigin);
    const AreaState *destination=nullptr;Vec offset;
    for(const auto &[region,candidate]:ports_.areas.all()) {
        (void)region;
        if(candidate.definition.act!=source.definition.act ||
            std::none_of(navigation.navigationSources.begin(),navigation.navigationSources.end(),
                [&](const auto &entry){return entry.grid==&candidate.definition.collision;})) continue;
        const Vec translation=source.definition.origin-candidate.definition.origin;
        if(!candidate.definition.activation.room(actor.position+translation+center)) continue;
        if(destination) return false;
        destination=&candidate;offset=translation;
    }
    if(!destination) return false;
    // PATH_RecacheRoomAtCoordIfNeeded moves room ownership without restarting
    // the path or treating a continuous level boundary as a pet teleport.
    actor.area=destination->definition.id;
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
    if((pathType!=1 && pathType!=2 && pathType!=7 && pathType!=13 && pathType!=15) || !pathDistance)
        return {DomainStatus::InvalidRequest,{}};
    const Vec center{.5f,.5f};
    auto pathGrid=ports_.spatial.navigationGrid(actor.area,actor.id,
        !request.circleTurn && (pathType==1 || pathType==7 || pathType==15)?request.target:EntityId{});
    // A retained WallFollow path still needs its unit-target search bound.
    auto route = request.circleTurn?pathGrid.nativeMonsterCirclePath(actor.position+center,request.destination.position+center,request.circleDistance,request.circleTurn,collision):
        pathGrid.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision,pathType==15 && bool(request.target));
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
    actor.pathStep.reset();
    actor.pathSteps=uint8_t(request.pathSteps);
    actor.pathType=pathType;actor.pathDistance=pathDistance;
    actor.movementGoal = goal;
    actor.stopDistance = request.pathSteps>0?request.pathSteps-1:request.stopDistance;
    actor.velocityPercent = request.velocityPercent; actor.running = request.running;
    if(!actor.airborne) {actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;}
    ++actor.revision;
    actor.movementRevision=actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        // Step::PATH_ResetMovement clears points and velocity, not pTargetUnit.
        if (!actor.route.empty() || actor.moving || actor.running) { actor.route.clear(); actor.moving = false; actor.running = false; ++actor.revision; }
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
        actor.modeFrame=tick.tick;
        if (!ports_.areas.loaded(actor.area, actor.position+Vec{.5f,.5f})) {
            actor.moving = false; actor.route.clear(); actor.movementTarget = {};
            continue;
        }
        if(!actor.nativeAiResetTracked) {
            if(actor.curseControl.ai!=CurseAi::None && actor.curseControl.until<=tick.tick) actor.curseControl={};
            if(actor.attractionUntil<=tick.tick) actor.attractionTarget={};
        }
        (void)id; if(actor.nativeAi()) actor.moving = false;
        if(actor.conversion && (actor.life<=0 || !actor.owner || !ports_.players.find(*actor.owner) || !ports_.players.find(*actor.owner)->entered)) {
            const auto result=ports_.effects.endConversion(id,tick.tick);
            if(!result) {blocked=true;continue;}
        }
        if(actor.knockbackGoal && actor.life>0) {
            const auto grid=ports_.spatial.navigationGrid(actor.area,actor.id);
            const Vec delta=*actor.knockbackGoal-actor.position;
            const Vec next=actor.position+delta.unit()*std::min(delta.length(),25.f*TickContext::seconds);
            if(grid.segment(actor.position+Vec{.5f,.5f},next+Vec{.5f,.5f},{},actor.rule.collision)) {actor.position=next;ports_.areas.moveRoomUnit(id,actor.area,actor.position);refreshCollision(actor);}
            else actor.knockbackGoal=actor.position;
            ++actor.revision;
            if(tick.tick<actor.knockedUntil) continue;
            actor.knockbackGoal.reset();actor.knockedUntil=0;
        }
        if(actor.holyFreezeUntil && actor.holyFreezeUntil<=tick.tick) actor.holyFreezeUntil=0;
        if(!actor.nativeAi()) continue; // Scripted native NPC AI owns its path; quest escapes use npc::System.
        if(actor.skillMotion && (actor.skillMotion->eventDriven || advanceSkillMotion(actor,tick.tick))) continue;
        if (frozen(id,tick.tick) || actor.stunnedUntil>tick.tick) continue;
        if (!actor.alive() || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const auto target=actor.movementTarget ? nativePathTarget(ports_.players,*this,actor.movementTarget,actor.area) : std::optional(std::pair{actor.movementGoal, 0});
        if(!target) {stop(id);continue;}
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        AreaStore::CollisionMove collisionMove{ports_.areas,actor.id,actor.area};
        const auto movementGrid=ports_.spatial.movementGrid(actor.area,collisionMove);
        auto collision=(actor.airborne || actor.submerged)?MovementCollisionRule{0,1}:actor.rule.collision;
        if(collision.mask && actor.rule.stats && actor.rule.stats->pd2) {
            const auto states=ports_.effects.unitStates(actor.id,tick.tick);
            if(std::any_of(states.begin(),states.end(),pd2PhasingState)) collision.mask=pd2PhasingMovementMask(true);
        }
        auto arrived=[&](Vec position) {
            // Coordinate paths finish by consuming their last precise point,
            // not by an extra floating stopping radius.
            if (!actor.movementTarget) return false;
            return meleeDistance(position,actor.rule.size,target->first,target->second)<=actor.stopDistance &&
                (actor.pathSteps || grid.segment(position,target->first));
        };
        if (arrived(actor.position)) { stop(id); continue; }
        const int speed = movementPercent(actor,tick.tick);
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, speed) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance == 0) { actor.route.pop_front(); continue; }
            constexpr Vec center{.5f,.5f};
            const auto previousStep=actor.pathStep;
            const auto step=actor.pathStep.advance(movementGrid,actor.position+center,actor.route.front()+center,
                remaining,collision);
            const Vec next=step.position-center;
            const bool atGoal=arrived(next);
            const auto previous=actor.position;actor.position=next;
            if(!recacheHirelingRoom(actor)) {actor.position=previous;actor.pathStep=previousStep;collisionMove.rollback();break;}
            ports_.areas.moveRoomUnit(id,actor.area,actor.position);refreshCollision(actor);
            actor.moving=actor.moving || step.travelled>0;++actor.revision;
            if(step.blocked) {actor.route.clear();break;}
            if (atGoal) { actor.route.clear(); break; }
            if(!step.reached) break;
            remaining-=step.travelled;actor.route.pop_front();
            if(step.travelled>0) break; // D2Common_10226: one nonzero path point per frame.
        }
        collisionMove.commit();
        // Path exhaustion/collision returns FALSE from D2Common_10226 and
        // leaves WALK/RUN in this frame; do not publish a stale chase target.
        if(actor.route.empty()) {recordNativeMode(actor,1);actor.moving=false;actor.running=false;}
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
} // namespace d2x::server::monsters
