#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::monsters {
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
