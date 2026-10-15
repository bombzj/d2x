#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x::server::objects {
DomainResult<> System::spawnTrap(Object &object,TickContext tick) {
    const auto &area=ports_.areas.at(object.area);if(area.definition.town) return {DomainStatus::Applied,std::monostate{}};
    if(object.trap==11) {
        const auto group=area.definition.environmentGroups.find("urn");if(group==area.definition.environmentGroups.end()) return {DomainStatus::Applied,std::monostate{}};
        const std::string key="object.urn."+std::to_string(object.id.value);
        while(object.spawned<int(group->second.size())) {
            auto unit=group->second[size_t(object.spawned)];unit.identity.spawnKey=key+(object.spawned?".minion."+std::to_string(object.spawned):"");if(object.spawned) unit.identity.ownerSpawnKey=key;
            const auto at=area.definition.collision.nativeSpawn(object.position+Vec{float(object.spawned%3-1),float(object.spawned/3)},8,unit.rule.spawnCollision);
            if(!at) {++object.spawned;continue;}
            const auto result=ports_.monsters.admit({unit.identity,unit.implementation,object.area,*at,true,unit.rule,{},tick.tick});if(!result) return {result.status,{}};++object.spawned;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(object.trap==5 || object.trap==7) {
        constexpr int fire[]{162,160};
        while(object.spawned<2) {
            auto result=admit({fire[object.spawned],object.area,object.position+Vec{float(object.spawned),0},{},{},1,tick.tick});
            if(!result) return {result.status,{}};
            ++object.spawned;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    int trap=object.trap;
    const int level=int(object.area);
    if((trap==8 && level>=75) || (trap==3 && level<40 && level!=25) || ((trap==1 || trap==4) && level<40)) trap=2;
    const bool monster=trap>=8;
    std::string code=trap==1?"trap-lightning":trap==3?"trap-poisoncloud":trap==4?"trap-nova":"trap-firebolt";
    if(monster) code=object.trap==10?(limitedRandom(object.random,2)?"trappedsoul2":"trappedsoul1"):"chest";
    const auto entry=area.definition.environmentMonsters.find(code);if(entry==area.definition.environmentMonsters.end()) return {DomainStatus::Unavailable,{}};
    if(code=="chest" && area.definition.act==0 && entry->second.identity.monster=="flyingscimitar") return {DomainStatus::Applied,std::monostate{}};
    if(!object.spawnCount) object.spawnCount=object.trap==8 || object.trap==9?1+int(limitedRandom(object.random,2)):1;
    while(object.spawned<object.spawnCount) {
        auto prepared=entry->second;prepared.identity.origin=SpawnOrigin::Summoned;
        prepared.identity.spawnKey="object."+std::to_string(object.id.value)+"."+std::to_string(object.spawned);
        if(object.trap==11 && prepared.rule.shrineUpgrade) {prepared.identity.rank=MonsterRank::Unique;prepared.rule=*prepared.rule.shrineUpgrade;}
        const auto position=area.definition.collision.nativeSpawn(object.position,8,prepared.rule.spawnCollision);
        if(!position) return {DomainStatus::Applied,std::monostate{}};
        const auto result=ports_.monsters.admit({prepared.identity,prepared.implementation,object.area,*position,true,prepared.rule,{},tick.tick});
        if(!result) return {result.status,{}};
        ++object.spawned;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::trapStep(TickContext tick) {
    bool blocked=false;
    for(auto &[id,object]:state_.objects) {
        if (!ports_.areas.loaded(object.area, object.position)) continue;
        (void)id;if(!object.trapAt) continue;
        bool active=false;const auto &area=ports_.areas.at(object.area);
        for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==object.area && area.definition.activation.nearby(p.position,object.position)) active=true;}
        if(!active) continue;
        if(!object.trapWarned && object.trap<=9) {
            if(!ports_.events.hasCapacity(1)) {blocked=true;continue;}
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{object.id,2,object.area,13}}});if(!sent) {blocked=true;continue;}object.trapWarned=true;
        }
        if(tick.tick<object.trapAt) continue;
        const auto result=spawnTrap(object,tick);if(result) {object.trapAt=object.secondaryTrap?tick.tick:0;object.trap=object.secondaryTrap;object.secondaryTrap=0;object.spawned=object.spawnCount=0;}
        else blocked|=result.status==DomainStatus::Capacity;
    }
    using Trap=MonsterRule::Trap;
    for(const auto &[id,monster]:ports_.monsters.read().actors) {
        if (!ports_.areas.loaded(monster.area, monster.position)) continue;
        const auto &rule=monster.rule;if(rule.trap==Trap::None || monster.life<=0) continue;
        auto &clock=traps_[id];if(clock.release && clock.interruption!=monster.interruption) {clock.release=0;clock.next=monster.busyUntil;}const auto &area=ports_.areas.at(monster.area);if(area.definition.town) continue;
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
                missiles::MonsterSpawn request{id,monster.area,area.generation,tick.tick,monster.position,clock.aim,attack,rule.hitStates,rule.level,rule.criticalChance,0,monster.combatRandom};
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
                if(launched) ports_.monsters.commitCombatRandom(id,launched.value->random);
            } else if(attack.maximum>0) {
                combat::Damage damage{id,clock.target,DamageType::Physical,attack.minimum,attack.maximum,monster.revision,monster.area,tick.tick,attack.rating,rule.level,rule.meleeRange,rule.size};
                damage.monsterMode=clock.mode;damage.sourceInterruption=monster.interruption;damage.actionRandom=monster.combatRandom;result=ports_.combat.enqueue(damage);
            }
            if(!result && result.status==DomainStatus::Capacity) {blocked=true;continue;}
            clock.release=0;continue;
        }
        if(clock.next>tick.tick || monster.busyUntil>tick.tick) continue;
        const auto before=clock;
        uint8_t mode=4;uint64_t delay=15;bool fire=false;Vec aim=target->position;
        if(rule.trap==Trap::Missile || rule.trap==Trap::Poison || rule.trap==Trap::Nova) {
            if(distance>rule.trapParameters[0] || clock.shots>=rule.trapParameters[1]) {const auto result=ports_.monsters.finishTrap(id,tick.tick);blocked|=result.status==DomainStatus::Capacity;continue;}
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
        } else if(rule.trap==Trap::Soul) {
            if(distance<5) {
                if(!clock.shots) {mode=9;++clock.shots;}
                else if(distance<=rule.meleeRange && tick.tick>clock.delayUntil) {const auto delta=target->position-monster.position;mode=delta.x<=0 && delta.y>=0?4:delta.x<0 || delta.y>0?8:5;clock.delayUntil=tick.tick+(mode==8?5:35);}
                else mode=8;
                fire=true;
            } else if(clock.shots) {mode=8;fire=true;}
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
