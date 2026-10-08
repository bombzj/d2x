#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
namespace d2x::server::missiles {
DomainResult<MonsterLaunch> System::spawnMonster(const MonsterSpawn &request) {
    const auto *source=ports_.monsters.find(request.source);const auto *area=ports_.areas.find(request.area);
    if(!source || source->owner || source->life<=0 || source->area!=request.area || !area || area->generation!=request.generation || area->definition.town ||
        !request.attack.missile || !std::isfinite(request.target.x) || !std::isfinite(request.target.y)) return {DomainStatus::InvalidActor,{}};
    const auto &rule=*request.attack.missile;
    if(rule.definition<0 || rule.frames<=0 || rule.speed<0 || (rule.speed==0 && rule.behavior!=MonsterMissileRule::Behavior::SpiderGoo) || request.extraQuills<0 || request.extraQuills>255) return {DomainStatus::InvalidRequest,{}};
    const size_t count=size_t(request.extraQuills)+1;
    if(count>4096-state_.missiles.size() || count>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    auto random=request.random;
    std::vector<Vec> aims{request.target};
    auto scatter=initialRandom(0x53454953); // MonsterMode seeds extra quills with SEIS.
    auto extras=monsterQuillTargets(request.target,request.extraQuills,scatter);aims.insert(aims.end(),extras.begin(),extras.end());
    std::vector<Missile> launched;
    for(size_t index=0;index<aims.size();++index) {
        const Vec aim=aims[index];
        const auto &rule=index && request.attack.extraQuill?*request.attack.extraQuill:*request.attack.missile;
        Missile missile;missile.owner=request.source;missile.emitter=request.source;missile.emitterType=1;
        missile.area=request.area;missile.generation=request.generation;missile.created=request.tick;
        missile.expires=request.tick+uint64_t(rule.frames);missile.lifetimeFrames=rule.frames;missile.definition=rule.definition;
        missile.position=request.position;missile.turnTarget=aim-request.position;
        if(!missile.turnTarget.length()) missile.turnTarget={1,1};
        missile.velocity=missile.turnTarget.unit()*rule.speed;missile.collision=rule.collision;missile.random=childRandom(random);
        if(rule.canSlow && rule.speed>0) {
            const int slow=ports_.effects.unitModifiers(request.source,request.tick).combat.slowMissiles;
            if(slow) {const auto fixed=missileVelocityFixed(rule.baseVelocity,rule.levelVelocity,rule.rank,slow);if(!fixed) return {DomainStatus::InvalidRequest,{}};missile.velocity=missile.velocity.unit()*(float(*fixed)*25.f/4096.f);}
        }
        auto hit=rollMonsterHit(request.attack.minimum,request.attack.maximum,0,request.attack.elements,rule.sourceDamage,missile.random);
        auto roll=[&](int minimum,int maximum) {return int64_t(minimum)+(maximum>minimum?limitedRandom(missile.random,unsigned(maximum-minimum)):0);};
        hit.channels[0]+=roll(rule.minimum,rule.maximum);
        hit.channels[size_t(rule.element)]+=roll(rule.elementalMinimum,rule.elementalMaximum);
        hit.coldFrames+=rule.coldFrames;hit.poisonFrames+=rule.poisonFrames;
        monsterCritical(hit,request.critical,missile.random);
        if(request.web) {hit.slowFrames=unsigned(request.web->slowFrames);hit.slowPercent=request.web->slowPercent;}
        missile.enemy=EnemyProjectile{rule,hit,request.states,request.level,request.attack.rating,request.web};
        launched.push_back(std::move(missile));
    }
    auto facts=visuals(launched);
    if(!facts.empty() && !ports_.events.hasCapacity(facts.size())) return {DomainStatus::Capacity,{}};
    std::map<EntityId,Missile> staged;auto cursor=ports_.ids.cursor();
    for(auto &missile:launched) {missile.id=EntityId{cursor++};staged.emplace(missile.id,std::move(missile));}
    const auto first=staged.begin()->first;
    if(!facts.empty() && !ports_.events.publish({0,request.tick,{}, {AudienceKind::Area,{},request.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    for(size_t i=0;i<count;++i) ports_.ids.allocate();
    state_.missiles.merge(staged);
    return {DomainStatus::Applied,MonsterLaunch{first,random}};
}
System::Advance System::advanceMonster(const Missile &original) const {
    Advance plan{original,{},{},false};auto &missile=plan.next;const auto &enemy=*missile.enemy;
    const auto &area=ports_.areas.at(missile.area);++missile.ageFrames;
    using Behavior=MonsterMissileRule::Behavior;
    const bool goo=enemy.rule.behavior==Behavior::SpiderGoo,lay=enemy.rule.behavior==Behavior::SpiderLay;
    Vec next=missile.position+missile.velocity*TickContext::seconds;
    const auto wall=goo?std::optional<float>{}:missileTerrainContact(area.definition.collision,missile.position,next,missile.collision);
    if(wall) next=missile.position+(next-missile.position)* *wall;
    const bool expired=missile.ageFrames>=missile.lifetimeFrames;
    struct Target {EntityId id;Vec position;int size;};std::vector<Target> targets;
    if(enemy.rule.collidePlayers) for(const auto &[key,player]:ports_.players.all()) {
        (void)key;if(player.entered && player.area==missile.area && player.persistent.player.hp>0) targets.push_back({player.actor,player.position,2});
    }
    if(enemy.rule.collideMonsters) for(const auto &[id,pet]:ports_.monsters.read().actors)
        if(pet.amazonPet && pet.life>0 && pet.area==missile.area) targets.push_back({id,pet.position,pet.rule.size});
    std::vector<std::pair<float,EntityId>> contacts;
    if(missile.ageFrames>enemy.rule.activate && !expired) for(const auto &target:targets) {
        if(!goo && missile.weaponContacts.contains(target.id)) continue;
        if(const auto fraction=missileUnitIntersection(missile.position,next,missile.collision.size,target.position,target.size)) contacts.emplace_back(*fraction,target.id);
    }
    std::sort(contacts.begin(),contacts.end());const Vec start=missile.position;
    auto impact=[&](std::vector<EntityId> units) {
        combat::SpellImpact hit{missile.id,missile.owner,missile.area,DamageType::Physical,0,std::move(units)};
        hit.occurrence=(uint64_t(missile.ageFrames)<<32)|missile.weaponContacts.size();
        hit.contactRandom=childRandom(missile.random);
        hit.monsterHit=enemy.hit;hit.monsterStates=enemy.states;hit.monsterLevel=enemy.level;hit.monsterRating=enemy.rating;hit.monsterToHit=enemy.rule.toHit;
        hit.returnFire=enemy.rule.returnFire;hit.hitClass=uint8_t(enemy.rule.hitClass);hit.nextDelay=unsigned(enemy.rule.nextDelay);
        if(enemy.rule.behavior==Behavior::FireHead) hit.sourceHeal=enemy.hit.channels[size_t(enemy.rule.element)];
        plan.impacts.push_back(std::move(hit));
    };
    if(lay) {
        if(!contacts.empty()) next=start+(next-start)*contacts.front().first;
        if(expired || wall || !contacts.empty()) {
            if(enemy.web) {
                const auto &ground=enemy.web->ground;Missile child=missile;child.id={};child.position=next;child.velocity={};child.ageFrames=0;
                child.definition=ground.definition;child.lifetimeFrames=ground.frames;child.collision=ground.collision;
                child.weaponContacts.clear();child.enemy->rule=ground;plan.children.push_back(std::move(child));
            }
            plan.finished=true;
        }
    } else if(!goo && enemy.rule.behavior==Behavior::Fireball && (wall || (expired && enemy.rule.alwaysExplode) || !contacts.empty())) {
        if(!contacts.empty()) next=start+(next-start)*contacts.front().first;
        std::vector<EntityId> units;
        for(const auto &target:targets) if((target.position-next).length()<=enemy.rule.blastRadius && area.definition.collision.missileSegment(next,target.position,{4,1})) units.push_back(target.id);
        if(!units.empty()) impact(std::move(units));
        plan.finished=true;
    } else if(goo && !expired) {
        std::vector<EntityId> units;for(const auto &[fraction,id]:contacts) { (void)fraction;units.push_back(id); }
        if(!units.empty()) impact(std::move(units));
    } else if(!expired) {
        // Persistent goo renews its target state while occupied. A traveling
        // projectile records contacts independently of each reliable hit batch.
        for(const auto &[fraction,id]:contacts) {
            impact({id});if(!goo) missile.weaponContacts.insert(id);
            if(enemy.rule.killOnHit) {next=start+(next-start)*fraction;plan.finished=true;break;}
        }
    }
    missile.position=next;if(expired || wall) plan.finished=true;
    ++missile.revision;return plan;
}
}
