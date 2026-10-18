#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/population/system.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x::server::objects {
DomainResult<> System::scheduleTrap(Object &object,uint64_t tick) {
    if(!object.trap || object.trapWarned) return {DomainStatus::Applied,std::monostate{}};
    if(object.trap<0 || object.trap>9) return {DomainStatus::NotImplemented,{}};
    const auto &area=ports_.areas.at(object.area).definition;
    if((object.trap==8 || object.trap==9) && area.act==0 && area.populationRules.chestBaseClass==234) {
        object.trap=0;object.trapAt=0;return {DomainStatus::Applied,std::monostate{}};
    }
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{object.id,2,object.area,13}}});
    if(!sent) return {sent.status,{}};
    object.trapAt=tick+35;object.trapWarned=true;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::spawnTrap(Object &object,TickContext tick) {
    if(object.trap<1 || object.trap>9) return {DomainStatus::NotImplemented,{}};
    const auto &area=ports_.areas.at(object.area);if(area.definition.town) return {DomainStatus::Applied,std::monostate{}};
    if(object.trap==5 || object.trap==7) {
        constexpr int fire[]{162,160};
        while(object.spawned<2) {
            if(object.spawned==1) {
                const auto *room=area.definition.activation.room(object.position);
                const int column=int(object.position.x)+1;
                if(!room || column<room->x || column>=room->x+room->width) {++object.spawned;continue;}
            }
            auto result=admit({fire[object.spawned],object.area,object.position+Vec{float(object.spawned),0},{},{},1,tick.tick});
            if(!result) return {result.status,{}};
            state_.objects.at(*result.value).sparkChest=2;
            ++object.spawned;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    int trap=object.trap;
    const int level=int(object.area);
    if((trap==8 && level>=75) || (trap==3 && level<40 && level!=25) || ((trap==1 || trap==4) && level<40)) trap=2;
    if(trap==8 || trap==9) {
        const auto &rules=area.definition.populationRules;
        if(area.definition.act==0 && rules.chestBaseClass==234) return {DomainStatus::Applied,std::monostate{}};
        if(rules.chestClass<0 || rules.chestBirthTicks<=0) return {DomainStatus::NotImplemented,{}};
        if(!object.spawnCount) object.spawnCount=1+int(limitedRandom(*state_.controlRandom,2));
        while(object.spawned<object.spawnCount) {
            const auto result=ports_.population.requestObjectBirth(object.area,object.id,object.position,tick,
                rules.chestClass,unsigned(object.spawned+1),1,3,rules.chestBirthTicks);
            if(!result) return {result.status==DomainStatus::Conflict?DomainStatus::Capacity:result.status,{}};
            ++object.spawned;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    const std::string code=trap==1?"trap-lightning":trap==3?"trap-poisoncloud":trap==4?"trap-nova":"trap-firebolt";
    const auto entry=area.definition.environmentMonsters.find(code);if(entry==area.definition.environmentMonsters.end()) return {DomainStatus::Unavailable,{}};
    auto prepared=entry->second;
    prepared.identity.origin=SpawnOrigin::Summoned;
    prepared.identity.spawnKey="object.trap.controller."+std::to_string(object.id.value);
    const auto birth=ports_.monsters.prepareTrapController({prepared.identity,prepared.implementation,object.area,
        object.position,true,prepared.rule,{},tick.tick},ports_.spatial.nativeCollisionSample());
    if(!birth) return {birth.status,{}};
    if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
    (*birth.value)->publish();
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::objectTrap(TickContext tick,EntityId only) {
    bool blocked=false;
    for(auto entry=state_.objects.find(only);entry!=state_.objects.end() && entry->first==only;++entry) {
        auto &[id,object]=*entry;
        if (!ports_.areas.loaded(object.area, object.position)) continue;
        (void)id;if(!object.trapAt) continue;
        const auto &area=ports_.areas.at(object.area);
        if((object.trap==8 || object.trap==9) && area.definition.act==0 && area.definition.populationRules.chestBaseClass==234) {
            object.trapAt=0;object.trap=0;object.spawned=object.spawnCount=0;continue;
        }
        if(tick.tick<object.trapAt) continue;
        const auto result=spawnTrap(object,tick);if(result) {object.trapAt=0;object.trap=0;object.spawned=object.spawnCount=0;}
        else if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) blocked=true;
        else return StepStatus::NotImplemented;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::monsterTrap(TickContext tick,EntityId only) {
    bool blocked=false;
    using Trap=MonsterRule::Trap;
    for(auto entry=ports_.monsters.read().actors.find(only);entry!=ports_.monsters.read().actors.end() && entry->first==only;++entry) {
        const auto &[id,monster]=*entry;
        if(!ports_.events.hasCapacity(4,2)) return StepStatus::Blocked;
        if (!ports_.areas.loaded(monster.area, monster.position)) continue;
        const auto &rule=monster.rule;if(rule.trap==Trap::None || rule.ai.kind==MonsterAiKind::TrappedSoul || monster.life<=0) continue;
        auto &clock=traps_[id];
        if(clock.admissionRevision!=monster.admissionRevision) {clock={};clock.admissionRevision=monster.admissionRevision;}
        if(clock.release && clock.interruption!=monster.interruption) {clock.release=0;clock.next=monster.busyUntil;}const auto &area=ports_.areas.at(monster.area);if(area.definition.town) continue;
        const PlayerState *target=nullptr;float distance=std::numeric_limits<float>::max();
        for(const auto &[key,p]:ports_.players.all()) {
            (void)key;if(!p.entered || p.area!=monster.area || p.persistent.player.hp<=0 || !area.definition.activation.nearby(p.position,monster.position)) continue;
            const float candidate=meleeDistance(monster.position,rule.size,p.position,2);
            if(candidate<distance) {distance=candidate;target=&p;}
        }
        if(!target || ports_.monsters.frozen(monster.id,tick.tick) || monster.stunnedUntil>tick.tick || monster.knockedUntil>tick.tick) continue;
        if(clock.release) {
            if(tick.tick<clock.release) continue;
            const auto &attack=rule.attacks.at(clock.mode);
            DomainResult<> result{DomainStatus::Applied,std::monostate{}};
            if(attack.missile) {
                missiles::MonsterSpawn request{id,monster.area,area.generation,tick.tick,monster.position,clock.aim,attack,rule.hitStates,rule.level,rule.criticalChance,0};
                request.environment=rule.environmentController;
                if(rule.trap==Trap::Nova) for(const auto direction:missileRingBurst(1)) request.aims.push_back(monster.position+direction);
                if(rule.trap==Trap::Poison) for(const auto &heading:poisonCloudDirections(2,2)) {request.aims.push_back(monster.position+heading.direction);request.speeds.push_back(heading.secondary?attack.missile->maximumVelocity:attack.missile->speed);}
                if(rule.trap==Trap::Turret) {
                    const Vec delta{float(int(clock.aim.x)-int(monster.position.x)),float(int(clock.aim.y)-int(monster.position.y))};
                    const Vec side{delta.y<0?2.f:delta.y>0?-2.f:0.f,delta.x<0?-2.f:delta.x>0?2.f:0.f};
                    Vec offset=delta-side*(float(rule.trapParameters[5])/2.f);
                    for(int i=0;i<rule.trapParameters[5];++i) {const auto origin=monster.position+Vec{float(int(offset.x)/6),float(int(offset.x)/6)};request.origins.push_back(origin);request.aims.push_back(origin+offset);offset=offset+side;}
                }
                const auto launched=ports_.missiles.spawnMonster(request);result={launched.status,launched?std::optional{std::monostate{}}:std::nullopt};
            } else if(attack.maximum>0) {
                result=ports_.combat.monsterMelee(id,clock.target,attack,tick.tick,{tick.tick});
            }
            if(!result && isDeferred(result.status)) {blocked=true;continue;}
            clock.release=0;continue;
        }
        if(clock.next>tick.tick || monster.busyUntil>tick.tick) continue;
        const auto before=clock;
        uint8_t mode=4;uint64_t delay=15;bool fire=false;Vec aim=target->position;
        if(rule.trap==Trap::Missile || rule.trap==Trap::Poison || rule.trap==Trap::Nova) {
            if(distance>rule.trapParameters[0] || clock.shots>=rule.trapParameters[1]) {const auto result=ports_.monsters.finishTrap(id,tick.tick);blocked|=isDeferred(result.status);continue;}
            if(clock.waiting) {clock.waiting=false;clock.next=tick.tick+uint64_t(std::max(1,rule.trapParameters[2]));continue;}
            fire=true;clock.waiting=true;++clock.shots;
            if(rule.trap==Trap::Nova) mode=8;
        } else if(rule.trap==Trap::RightArrow || rule.trap==Trap::LeftArrow) {
            if(distance<rule.trapParameters[0] || distance>rule.trapParameters[1]) {clock.next=tick.tick+40;continue;}
            const auto delta=target->position-monster.position;
            if(std::abs(rule.trap==Trap::RightArrow?int(delta.x):int(delta.y))>2 || tick.tick<=clock.delayUntil) {clock.next=tick.tick+30;continue;}
            auto [variant,fresh]=arrowVariants_.try_emplace(monster.area,0);
            if(fresh && int(monster.area)>=40) {auto random=monster.combatRandom;variant->second=int(limitedRandom(random,3));ports_.monsters.commitCombatRandom(id,random);}
            mode=variant->second==1?8:4;fire=rule.attacks.contains(mode);clock.delayUntil=tick.tick+uint64_t(std::max(1,rule.trapParameters[2]));
        } else if(rule.trap==Trap::Melee) {
            if(distance>rule.meleeRange) {clock.next=tick.tick+40;continue;}
            auto random=monster.combatRandom;fire=int(limitedRandom(random,100))<rule.trapParameters[0];ports_.monsters.commitCombatRandom(id,random);delay=uint64_t(std::max(1,rule.trapParameters[1]));
        } else if(rule.trap==Trap::Turret) {
            if(distance>rule.trapParameters[3]) {clock.shots=std::max(0,clock.shots-1);clock.next=tick.tick+15;continue;}
            constexpr int turns[]{0,0,1,2,5,6,7,0,1,1,1,2,3,6,7,0,1,2,2,2,3,4,7,0,1,2,3,3,3,4,5,6,1,2,3,4,4,4,5,6,7,2,3,4,5,5,5,6,7,0,3,4,5,6,6,6,7,0,1,4,5,6,7,7};
            constexpr Vec offsets[]{{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1},{1,0}};
            clock.direction=turns[clock.direction+8*monsterFacing8(monster.position,target->position)];
            aim=monster.position+offsets[clock.direction]*float(rule.trapParameters[4]);
            if(!area.definition.collision.collisionSegment(monster.position,aim,0x0804) || !area.definition.collision.collisionSegment(monster.position,target->position,0x0804)) {clock.shots=std::max(0,clock.shots-1);clock.next=tick.tick+10;continue;}
            mode=8;fire=true;delay=uint64_t(std::max(1,++clock.shots<=rule.trapParameters[1]?rule.trapParameters[0]:rule.trapParameters[2]));if(clock.shots>rule.trapParameters[1]) clock.shots=0;
        }
        if(!fire || !rule.attacks.contains(mode)) {clock.next=tick.tick+delay;continue;}
        const auto &attack=rule.attacks.at(mode);
        if(!ports_.events.hasCapacity(1)) {clock=before;blocked=true;continue;}
        const bool skill=rule.trap==Trap::Poison || rule.trap==Trap::Nova || rule.trap==Trap::Turret || ((rule.trap==Trap::RightArrow || rule.trap==Trap::LeftArrow) && mode==8);
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},monster.area},{AttackFact{id,rule.trap==Trap::Turret?EntityId{}:target->actor,1,0,monster.area,monster.position,aim,monster.revision,uint16_t(skill?rule.skillIds[0]:0),attack.rank,false,mode}}});
        if(!sent) {clock=before;blocked=true;continue;}
        const auto result=ports_.monsters.beginAttack(id,tick.tick+uint64_t(attack.duration),false,
            rule.trap==Trap::Turret?EntityId{}:target->actor);if(!result) continue;
        clock.interruption=monster.interruption;clock.mode=mode;clock.aim=aim;clock.target=target->actor;clock.release=tick.tick+uint64_t(attack.release);clock.next=tick.tick+uint64_t(attack.duration)+delay;
    }
    std::erase_if(traps_,[&](const auto &entry){const auto *m=ports_.monsters.find(entry.first);return !m || m->life<=0;});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
