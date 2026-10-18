#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::monsters {
std::optional<Vec> System::leapAttackLanding(EntityId source,EntityId target,const std::function<uint16_t(RoomView,int32_t,int32_t)> &sample) const {
    const auto *actor=find(source);if(!actor || !actor->alive() || source==target) return {};
    const auto victim=targetPosition(target,actor->area);if(!victim) return {};
    const auto &area=ports_.areas.at(actor->area).definition;
    if(unitMeleeReach(area.collision,actor->position,actor->rule.size,victim->first,victim->second,actor->rule.meleeRange)) return {};
    const auto from=actor->position+area.origin,to=victim->first+area.origin;
    const int x=int(std::floor(from.x)),y=int(std::floor(from.y)),tx=int(std::floor(to.x)),ty=int(std::floor(to.y));
    const int divisor=unitCoordinateDistance(x,y,tx,ty);if(!divisor) return {};
    const auto room=ports_.areas.roomAtPosition(actor->area,actor->position,x,y);if(!room) return {};
    int landingX=tx+2*(tx-x)/divisor,landingY=ty+2*(ty-y)/divisor;
    auto destination=ports_.areas.roomAtPosition(actor->area,actor->position,landingX,landingY);
    if(!destination || ports_.areas.nativeCollisionMask(*destination,landingX,landingY,actor->rule.size,0x1c09,sample)) {
        const auto found=ports_.areas.nativeFreeCoordinates(*room,tx,ty,actor->rule.size,0x1c09,sample);
        if(!found) return {};destination=found->room;
        const auto point=found->position+ports_.areas.at(destination->area).definition.origin;
        landingX=int(point.x);landingY=int(point.y);
    }
    if(destination->area!=actor->area || ports_.areas.at(destination->area).definition.town) return {};
    const Vec landing=Vec{float(landingX),float(landingY)}-area.origin;
    if(!area.collision.collisionSegment(actor->position,landing,0x804)) return {};
    return landing;
}
DomainResult<> System::reserveLeapAttack(EntityId source,Vec target) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    // Retail St41 reserves PLAYER even for a monster. Landing resets MONSTER.
    ports_.areas.setNativePatternMask(actor.area,target,actor.rule.collisionPattern,0x80,true);
    actor.skillReservation=Actor::SkillReservation{target,0x100};++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startLeapAttackMotion(EntityId source) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end() || !found->second.skillReservation) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    actor.skillMotion=Actor::SkillMotion{actor.skillReservation->position,UINT64_MAX,actor.interruption,float(actor.rule.nativeRunVelocity)*25.f/16.f,true,{}};
    actor.skillMotion->eventDriven=true;actor.skillMotion->collision=MovementCollisionRule{0,actor.rule.size};
    actor.collisionPattern=NativeCollisionPattern::None;actor.pathStep.reset();actor.movementTarget={};
    actor.movementGoal=actor.skillReservation->position;actor.pathType=9;++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
std::optional<Vec> System::leapLanding(EntityId source,Vec target) const {
    const auto *actor=find(source);if(!actor || !actor->alive()) return {};
    const auto &grid=ports_.areas.at(actor->area).definition.collision;
    const MovementCollisionRule collision{0x3c01,actor->rule.collision.size};
    const auto free=[&](Vec point) {
        if(!grid.walkable(point,collision)) return false;
        for(const auto &[id,other]:state_.actors) if(id!=source && other.alive() && !other.burrowed && !other.airborne && !other.submerged && !other.hatching &&
            other.area==actor->area && meleeDistance(point,actor->rule.size,other.position,other.rule.size)<=0) return false;
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==actor->area && p.persistent.player.hp>0 && meleeDistance(point,actor->rule.size,p.position,2)<=0) return false;}
        return true;
    };
    if(free(target)) return target;
    const Vec origin{std::floor(target.x),std::floor(target.y)};
    for(int radius=1;radius<50;++radius) {
        std::optional<Vec> chosen;int best=INT32_MAX;
        const auto candidate=[&](int x,int y) {
            const Vec point=origin+Vec{float(x),float(y)};const int distance=std::abs(x)+std::abs(y);
            if(distance<best && free(point)) {best=distance;chosen=point;}
        };
        for(int y=-radius;y<=radius;++y) {candidate(-radius,y);candidate(radius,y);}
        for(int x=1-radius;x<radius;++x) {candidate(x,-radius);candidate(x,radius);}
        if(chosen) return chosen;
    }
    return {};
}
bool System::canMonsterLeap(EntityId source,Vec target) const {
    const auto *actor=find(source);if(!actor || !actor->alive()) return false;
    const auto &area=ports_.areas.at(actor->area).definition;
    const auto landing=target*2.f-actor->position;
    const auto chosen=leapLanding(source,landing);
    return !area.town && chosen && chosen->x==landing.x && chosen->y==landing.y &&
        area.collision.walkable(landing,{0x3c01,actor->rule.collision.size}) && area.collision.collisionSegment(actor->position,landing,0xc01);
}
DomainResult<> System::finishLeaperLanding(EntityId id,EntityId target,int skill,uint64_t tick) {
    auto &actor=state_.actors.at(id);const auto &area=ports_.areas.at(actor.area).definition;
    std::vector<DomainFact> facts;
    for(const int state:{actor.rule.uninterruptableState,actor.rule.skillMoveState})
        if(state>=0 && actor.aiStates.contains(state)) facts.emplace_back(StateFact{id,1,actor.area,state,false});
    facts.emplace_back(SkillEndFact{id,1,actor.area,uint16_t(skill)});
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
    if(!sent) return {sent.status,{}};
    actor.aiStates.erase(actor.rule.uninterruptableState);actor.aiStates.erase(actor.rule.skillMoveState);
    if(const auto position=targetPosition(target,actor.area)) {
        const Vec center{.5f,.5f};
        const Vec goal=actor.position*3.f-position->first*2.f;
        const auto clipped=area.collision.nativeMovementTarget(actor.position+center,goal+center,{0x3c01,actor.rule.collision.size})-center;
        if((clipped-actor.position).length()>.01f) {
            const float speed=float(actor.rule.nativeRunVelocity)*25.f/16.f;
            actor.skillMotion=Actor::SkillMotion{clipped,UINT64_MAX,actor.interruption,speed,false,{}};
            actor.skillMotion->leaperRecovery=true;actor.pathStep.reset();
        }
    }
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
}
