#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::monsters {
bool System::resurrectionTarget(EntityId source,EntityId corpse,uint64_t tick) const {
    const auto *caster=find(source),*dead=find(corpse);
    if(!caster || !dead || source==corpse || caster->life<=0 || dead->life>0 || dead->owner || !dead->rule.corpseSelectable ||
        dead->corpseUnavailable || tick<dead->busyUntil || !dead->rewardComplete || dead->identity.rank!=MonsterRank::Normal || dead->area!=caster->area) return false;
    // Ordinary Fn013 uses its party callback. The broader family callback is
    // reserved for unique shamans and is deliberately not used here.
    if(dead->identity.ownerSpawnKey!=caster->identity.spawnKey || dead->rule.ai.kind!=MonsterAiKind::Fallen) return false;
    const int radius=caster->rule.ai.params[3];
    // Fn013 deliberately passes a squared threshold to callback 9, whose
    // actual metric is FullUnitSize. Preserve that native quirk and room scope.
    return ports_.areas.at(caster->area).definition.activation.nearby(caster->position,dead->position) &&
        monsterAiDistance(caster->position,caster->rule.size,dead->position)<=radius*radius;
}
DomainResult<> System::resurrect(EntityId source,EntityId corpse,uint64_t tick) {
    if(!resurrectionTarget(source,corpse,tick)) return {DomainStatus::Unavailable,{}};
    auto &actor=state_.actors.at(corpse);const auto &grid=ports_.areas.at(actor.area).definition.collision;
    if(!grid.walkable(actor.position,actor.rule.collision)) return {DomainStatus::Unavailable,{}};
    for(const auto &[id,other]:state_.actors) if(id!=corpse && other.area==actor.area && other.life>0 && (actor.position-other.position).length()<1.f) return {DomainStatus::Unavailable,{}};
    for(const auto &[id,p]:ports_.players.all()) { (void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && (actor.position-p.position).length()<1.f) return {DomainStatus::Unavailable,{}}; }
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{HitFact{corpse,1,actor.area,128,false,actor.position}}})) return {DomainStatus::Capacity,{}};
    actor.life=actor.maximumLife;actor.chilledUntil=actor.frozenUntil=0;actor.poison.reset();
    actor.riseUntil=actor.busyUntil=tick+uint64_t(actor.rule.resurrectionTicks);actor.riseMode=8;
    actor.killer={};actor.route.clear();actor.moving=actor.running=false;actor.movementTarget={};
    actor.hitOccurrence=0;actor.knockedUntil=0;actor.knockbackGoal.reset();++actor.revision;
    // rewardComplete remains set: SKILLS_ResurrectUnit gives NOXP | NOTC.
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<EntityId> System::spawnNestChild(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || !it->second.rule.nestChild) return {DomainStatus::InvalidActor,{}};
    auto &nest=it->second;const auto &child=*nest.rule.nestChild;
    if(nest.nestSpawned>=nest.rule.ai.params[2]) return {DomainStatus::Unavailable,{}};
    const auto &grid=ports_.areas.at(nest.area).definition.collision;
    auto random=nest.combatRandom;
    const Vec origin=nest.position+nest.rule.spawnOffset;
    // master nestSpawn's perimeter walk; MPQ spawnx/spawny supply the origin.
    int x=0,y=0;
    if(rollRandom(random)&1) {y=3;x=int(limitedRandom(random,3));}else {x=3;y=int(limitedRandom(random,3));}
    if(rollRandom(random)&1) x=-x;
    if(rollRandom(random)&1) y=-y;
    for(int attempt=0;attempt<24;++attempt) {
        const Vec point=origin+Vec{float(x),float(y)};
        if(x==-3 && y<3) ++y;else if(y==3 && x<3) ++x;else if(x==3 && y>-3) --y;else --x;
        if(!grid.walkable(point,child.rule.spawnCollision)) continue;
        bool occupied=false;
        for(const auto &[key,actor]:state_.actors) { (void)key;if(actor.area==nest.area && actor.life>0 && (point-actor.position).length()<1.f) {occupied=true;break;} }
        for(const auto &[key,p]:ports_.players.all()) { (void)key;if(p.entered && p.area==nest.area && p.persistent.player.hp>0 && (point-p.position).length()<1.f) {occupied=true;break;} }
        if(occupied) continue;
        auto identity=child.identity;identity.spawnKey=nest.identity.spawnKey+".child."+std::to_string(nest.nestSpawned);identity.ownerSpawnKey=nest.identity.spawnKey;
        const auto admitted=admit({identity,child.implementation,nest.area,point,true,child.rule});
        if(admitted) {state_.actors.at(*admitted.value).rewardComplete=true;++nest.nestSpawned;nest.combatRandom=random;++nest.revision;}
        return admitted;
    }
    (void)tick;nest.combatRandom=random;return {DomainStatus::Unavailable,{}};
}
DomainResult<> System::activateWeb(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || !it->second.rule.web) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.rule.web->aura.id,true}}})) return {DomainStatus::Capacity,{}};
    actor.webUntil=tick+uint64_t(actor.rule.web->auraFrames);actor.webOrigin=actor.position;actor.webRandom=childRandom(actor.combatRandom);++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::heal(EntityId id,int64_t amount) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || amount<0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;actor.life=std::min(actor.maximumLife,actor.life+amount);++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::slow(EntityId id,int state,int percent,uint64_t frames,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || state<0 || state>=255 || percent< -100 || percent>0 || !frames) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(!actor.slowed && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,state,true}}})) return {DomainStatus::Capacity,{}};
    actor.slowed=Actor::Slow{state,percent,tick+std::min(frames,UINT64_MAX-tick)};++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
}
