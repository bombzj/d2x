#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x::server::monsters {
bool System::canLand(EntityId source) const {
    const auto *actor=find(source);if(!actor || actor->life<=0) return false;
    if(!ports_.areas.at(actor->area).definition.collision.walkable(actor->position,actor->rule.spawnCollision)) return false;
    for(const auto &[id,other]:state_.actors) if(id!=source && other.life>0 && !other.burrowed && !other.airborne && other.area==actor->area && meleeDistance(actor->position,actor->rule.size,other.position,other.rule.size)<=0) return false;
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;if(player.entered && player.area==actor->area && player.persistent.player.hp>0 && meleeDistance(actor->position,actor->rule.size,player.position,2)<=0) return false;
    }
    return true;
}
DomainResult<> System::aiPose(EntityId source,uint8_t mode,bool hold,bool airborne,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto clock=actor.rule.aiPoseTicks.find(mode);
    if(clock==actor.rule.aiPoseTicks.end()) return {DomainStatus::Unavailable,{}};
    if(actor.airborne && !airborne && !canLand(source)) return {DomainStatus::Unavailable,{}};
    actor.aiPose=mode;actor.holdPose=hold;actor.airborne=airborne;actor.poseUntil=tick+uint64_t(clock->second);
    if(!airborne) {stop(source);actor.busyUntil=std::max(actor.busyUntil,actor.poseUntil);}
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::aiColor(EntityId source,int color,int overlay,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    if(color<0 || color>2 || actor.rule.blueState<0 || actor.rule.redState<0 || overlay>UINT16_MAX) return {DomainStatus::InvalidRequest,{}};
    auto states=actor.aiStates;states.erase(actor.rule.blueState);states.erase(actor.rule.redState);
    if(color) states.insert(color==1?actor.rule.blueState:actor.rule.redState);
    std::vector<DomainFact> facts;
    for(int state:actor.aiStates) if(!states.contains(state)) facts.emplace_back(StateFact{source,1,actor.area,state,false});
    for(int state:states) if(!actor.aiStates.contains(state)) facts.emplace_back(StateFact{source,1,actor.area,state,true});
    if(overlay>=0) facts.emplace_back(OverlayFact{source,1,actor.area,overlay});
    if(facts.empty()) return {DomainStatus::Applied,std::monostate{}};
    const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
    if(!published) return {published.status,{}};
    actor.aiStates=std::move(states);++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::aiRegeneration(EntityId source,int eighths) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || eighths<0 || eighths>65535) return;
    auto &actor=found->second;const int64_t bonus=(actor.maximumLife*actor.rule.damageRegen/4096)*eighths/8;
    if(actor.aiRegenerationBonus!=bonus) {actor.aiRegenerationBonus=bonus;++actor.revision;}
}
bool System::mummySupportTarget(EntityId source,EntityId target,uint64_t tick,bool dead,bool healthy) const {
    const auto *caster=find(source),*other=find(target);
    if(!caster || !other || source==target || caster->life<=0 || caster->rule.ai.kind!=MonsterAiKind::GreaterMummy ||
       other->area!=caster->area || other->owner || other->conversion || !other->hostile) return false;
    // Fn022: ordinary greater mummies support lUndead; Radament supports both
    // undead classes and accepts unique corpses only on Normal difficulty.
    const bool radament=caster->identity.monster=="radament";
    if(radament?!other->rule.undead:!other->rule.lesserUndead) return false;
    if(!(radament && caster->rule.difficulty==0) && (other->identity.rank==MonsterRank::Unique ||
       other->identity.rank==MonsterRank::SuperUnique || other->identity.rank==MonsterRank::Champion)) return false;
    const int radius=caster->rule.ai.params[4]+(radament?10:0);
    const Vec delta{float(int(caster->position.x)-int(other->position.x)),float(int(caster->position.y)-int(other->position.y))};
    if(delta.x*delta.x+delta.y*delta.y>radius*radius || !ports_.areas.at(caster->area).definition.activation.nearby(caster->position,other->position)) return false;
    return dead?usableCorpse(target,caster->area,tick):other->life>0 && (healthy || other->life<other->maximumLife);
}
DomainResult<> System::spawnSkillChildren(EntityId source,Vec target,int count,bool hatch,uint64_t tick) {
    auto found=state_.actors.find(source);
    if(found==state_.actors.end() || found->second.life<=0 || !found->second.rule.nestChild || count<1 || count>12) return {DomainStatus::InvalidActor,{}};
    auto &parent=found->second;const auto &child=*parent.rule.nestChild;
    if(size_t(count)>65536-state_.actors.size() || uint64_t(count)>UINT32_MAX-ports_.ids.cursor() || !ports_.events.hasCapacity(4)) return {DomainStatus::Capacity,{}};
    const auto &grid=ports_.areas.at(parent.area).definition.collision;
    constexpr std::array directionIndices{10,8,22,20,18,16,14,12};
    const Vec origin=hatch?parent.position:parent.rule.ai.kind==MonsterAiKind::SandMaggotQueen?
        parent.position+parent.rule.spawnOffset:parent.position+monsterDirectionOffset(directionIndices[size_t(monsterFacing8(parent.position,target))]);
    int born=0;
    DomainStatus failure=DomainStatus::Applied;
    const auto place=[&](Vec point) {
        if(!grid.walkable(point,child.rule.spawnCollision)) return false;
        bool occupied=false;
        for(const auto &[id,other]:state_.actors) if(id!=source && other.area==parent.area && other.life>0 && !other.burrowed && !other.airborne && meleeDistance(point,child.rule.size,other.position,other.rule.size)<=0) {occupied=true;break;}
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==parent.area && p.persistent.player.hp>0 && meleeDistance(point,child.rule.size,p.position,2)<=0) occupied=true;}
        if(occupied) return false;
        auto identity=child.identity;identity.spawnKey=parent.identity.spawnKey+".skill-child."+std::to_string(parent.nestSpawned);identity.ownerSpawnKey=parent.identity.spawnKey;
        auto rule=child.rule;rule.noExperience=true;
        const auto admitted=admit({identity,child.implementation,parent.area,point,true,std::move(rule)});
        if(!admitted) {failure=admitted.status;return false;}
        auto &actor=state_.actors.at(*admitted.value);actor.riseMode=parent.rule.spawnMode;actor.riseUntil=actor.busyUntil=tick+uint64_t(parent.rule.spawnTicks);
        ++parent.nestSpawned;++parent.revision;++born;
        return true;
    };
    place(origin);
    if(failure!=DomainStatus::Applied) return {failure,{}};
    if(hatch) {
        // SrvDo084 first probes the origin. Subsequent SpawnNormalMonster
        // calls use search limits 1..4, translated to radii 3,6,9,12.
        // Only a failed complete search raises the limit; at most 2*count
        // calls follow the initial probe. Keep the parent's random advances.
        auto random=parent.combatRandom;
        int searchLimit=1;
        for(int attempt=0;attempt<2*count && born<count && searchLimit<5;++attempt) {
            bool placed=false;
            for(int radius=3;radius<=3*searchLimit && !placed;radius+=3) {
                const auto start=monsterWanderPoint(origin,radius,random)-origin;
                int x=int(start.x),y=int(start.y);
                for(int probe=0;probe<8*radius && !placed;++probe) {
                    if(y==-radius && x<radius) ++x;
                    else if(x==radius && y<radius) ++y;
                    else if(y==radius && x>-radius) --x;
                    else --y;
                    placed=place(origin+Vec{float(x),float(y)});
                    if(failure!=DomainStatus::Applied) return {failure,{}};
                }
            }
            if(!placed) ++searchLimit;
        }
        parent.combatRandom=random;
    }
    if(hatch) {
        // The hatch is terminal even if some larvae cannot be placed.
        const auto result=damage(source,source,parent.life,tick);if(!result) return result;
        parent.rewardComplete=true;parent.corpseUnavailable=true;
        return {DomainStatus::Applied,std::monostate{}};
    }
    return born?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Unavailable,{}};
}
DomainResult<> System::burrow(EntityId source,bool below,int healPercent,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    if(!below) {
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        if(!grid.walkable(actor.position,actor.rule.spawnCollision)) return {DomainStatus::Unavailable,{}};
        for(const auto &[id,other]:state_.actors) if(id!=source && other.life>0 && !other.burrowed && !other.airborne && other.area==actor.area && meleeDistance(actor.position,actor.rule.size,other.position,other.rule.size)<=0) return {DomainStatus::Unavailable,{}};
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && meleeDistance(actor.position,actor.rule.size,p.position,2)<=0) return {DomainStatus::Unavailable,{}};}
    }
    (void)tick;stop(source);actor.burrowed=below;
    if(below && healPercent>0) actor.life=std::min(actor.maximumLife,actor.life+actor.life*healPercent/100);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startSkillMotion(EntityId source,Vec target,uint64_t until,int velocity,bool leap,uint64_t tick,EntityId victim) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0 || until<=tick || velocity<1) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto &grid=ports_.areas.at(actor.area).definition.collision;
    const auto occupied=[&](Vec point) {
        for(const auto &[id,other]:state_.actors) if(id!=source && other.area==actor.area && other.life>0 && !other.burrowed && !other.airborne && meleeDistance(point,actor.rule.size,other.position,other.rule.size)<=0) return true;
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && meleeDistance(point,actor.rule.size,p.position,2)<=0) return true;}
        return false;
    };
    const auto free=[&](Vec point) {return grid.walkable(point,actor.rule.spawnCollision) && !occupied(point);};
    if(leap && !free(target)) {
        // COLLISION_GetFreeCoordinates probes successive square perimeters,
        // keeping the closest Manhattan candidate on the first free perimeter.
        const Vec origin{std::floor(target.x),std::floor(target.y)};
        bool found=false;
        for(int radius=1;radius<50 && !found;++radius) {
            int best=INT32_MAX;
            const auto candidate=[&](int x,int y) {
                const Vec point=origin+Vec{float(x),float(y)};
                const int distance=std::abs(x)+std::abs(y);
                if(distance<best && free(point)) {best=distance;target=point;found=true;}
            };
            for(int y=-radius;y<=radius;++y) {candidate(-radius,y);candidate(radius,y);}
            for(int x=1-radius;x<radius;++x) {candidate(x,-radius);candidate(x,radius);}
        }
        if(!found) return {DomainStatus::Unavailable,{}};
    } else if(!free(target)) return {DomainStatus::Unavailable,{}};
    if(!leap && !grid.nativeMovementSegment(actor.position,target,actor.rule.collision)) return {DomainStatus::Unavailable,{}};
    const int modifier=leap?0:actor.velocityPercent-100+(actor.chilledUntil>tick?actor.rule.coldEffect:0);
    const float speed=monsterMovementSpeed(actor.rule.nativeVelocity,velocity+modifier);
    if(speed<=0) return {DomainStatus::Unavailable,{}};
    if(actor.rule.skillMoveState>=0 && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{source,1,actor.area,actor.rule.skillMoveState,true}}})) return {DomainStatus::Capacity,{}};
    until=std::max(until,tick+uint64_t(std::ceil((target-actor.position).length()/speed/TickContext::seconds))+1);
    actor.skillMotion=Actor::SkillMotion{target,until,actor.interruption,speed,leap,{}};
    actor.skillMotion->target=victim;actor.skillMotion->moveState=actor.rule.skillMoveState>=0;
    actor.busyUntil=std::max(actor.busyUntil,until);actor.movementGoal=target;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startSwarmMove(EntityId source,Vec target,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto &grid=ports_.areas.at(actor.area).definition.collision;
    auto path=grid.path(actor.position,target,true,actor.rule.collision);
    if(path.empty()) return {DomainStatus::Unavailable,{}};
    std::deque<Vec> route;Vec previous=actor.position;float remaining=5.f;
    for(Vec point:path) {
        const Vec delta=point-previous;const float distance=delta.length();
        if(distance>remaining) {route.push_back(previous+delta.unit()*remaining);break;}
        route.push_back(point);remaining-=distance;previous=point;if(remaining<=0) break;
    }
    const float speed=monsterMovementSpeed(actor.rule.nativeVelocity,actor.velocityPercent+(actor.chilledUntil>tick?actor.rule.coldEffect:0));if(route.empty() || speed<=0) return {DomainStatus::Unavailable,{}};
    const auto until=tick+uint64_t(std::ceil(5.f/speed/TickContext::seconds))+1;
    actor.skillMotion=Actor::SkillMotion{route.back(),until,actor.interruption,speed,false,std::move(route)};
    actor.busyUntil=std::max(actor.busyUntil,until);++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
bool System::advanceSkillMotion(Actor &actor,uint64_t tick) {
    auto &motion=*actor.skillMotion;
    const bool interrupted=actor.life<=0 || actor.interruption!=motion.interruption || actor.frozenUntil>tick || actor.stunnedUntil>tick || actor.knockedUntil>tick;
    if(!interrupted) {
        if(motion.target) {
            const auto target=targetPosition(motion.target,actor.area);
            if(target && combat::Participants{ports_.players,*this}.canHarm(actor.id,motion.target)) {
                const Vec delta=target->first-actor.position;
                const float stop=float(actor.rule.meleeRange+3)+.5f;
                motion.destination=actor.position+delta.unit()*std::max(0.f,delta.length()-stop);
            } else motion.until=tick;
        }
        while(!motion.route.empty() && (motion.route.front()-actor.position).length()<.01f) motion.route.pop_front();
        const Vec delta=(motion.route.empty()?motion.destination:motion.route.front())-actor.position;
        const Vec next=actor.position+delta.unit()*std::min(delta.length(),motion.speed*TickContext::seconds);
        if(tick<motion.until && (motion.leap || ports_.areas.at(actor.area).definition.collision.nativeMovementSegment(actor.position,next,actor.rule.collision))) {actor.position=next;actor.moving=delta.length()>.01f;++actor.revision;}
        else motion.until=tick;
        if((motion.destination-actor.position).length()>.01f && tick<motion.until) return true;
    }
    if(motion.moveState && actor.rule.skillMoveState>=0 && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{actor.id,1,actor.area,actor.rule.skillMoveState,false}}})) return true;
    actor.skillMotion.reset();++actor.revision;return false;
}
}
