#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x::server::objects {
DomainResult<> System::shrineMissiles(const ActorContext &actor,Object &object) {
    const auto *player=ports_.players.find(actor.player);if(!player) return {DomainStatus::InvalidActor,{}};
    const auto &shrine=*object.rule.shrine;const auto &area=ports_.areas.at(object.area);
    const int rank=std::clamp(player->persistent.player.level/5,1,8);
    auto attack=shrine.projectiles.at(rank);
    std::vector<combat::SpellImpact> impacts;
    missiles::MonsterSpawn request{object.id,object.area,area.generation,actor.tick,object.position,object.position,attack,{},player->persistent.player.level,0,0,object.random};
    request.source=player->actor;request.playerSource=player->player;
    if(shrine.code==19) {
        const auto skill=skills::evaluate(*player,47,rank);
        if(skill.missileId!=62) return {DomainStatus::Unavailable,{}};
        request.attack.missile->elementalMinimum=int(skill.minimumDamage*256.f);request.attack.missile->elementalMaximum=int(skill.maximumDamage*256.f);
        combat::SpellImpact impact{object.id,object.id,object.area,DamageType::Physical,0,{}};
        impact.environment=true;impact.directLife=true;impact.environmentGeneration=area.generation;impact.occurrence=object.revision;
        auto add=[&](EntityId id,Vec at,int64_t life) {
            const Vec delta=at-object.position;
            if(life<=0 || !area.definition.activation.nearby(object.position,at) || delta.x*delta.x+delta.y*delta.y>float(shrine.argument1)*shrine.argument1) return;
            impact.targets.push_back(id);impact.targetDamage.push_back((life/256)*shrine.argument0/100*256);
        };
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==object.area) add(p.actor,p.position,int64_t(p.persistent.player.hp*256.f));}
        for(const auto &[id,m]:ports_.monsters.read().actors) if(m.area==object.area && m.damageable()) add(id,m.position,m.life);
        impacts.push_back(std::move(impact));
        for(int x=1;x<=4;++x) for(int y=1;y<=4;++y) request.aims.push_back(object.position+Vec{float((x&1?1:-1)*5*x),float((y&1?1:-1)*5*y)});
    } else {
        constexpr Vec offsets[]{{-6,6},{-6,-6},{0,6},{0,-6},{6,6},{6,-6}};
        for(const auto offset:offsets) request.aims.push_back(object.position+offset);
    }
    request.states=shrine.hitStates;
    auto plan=ports_.combat.prepareSpells(std::move(impacts));if(!plan) return {plan.status,{}};
    const auto launched=ports_.missiles.spawnMonster(request);if(!launched) return {launched.status,{}};
    ports_.combat.commitSpells(std::move(*plan.value));object.random=launched.value->random;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::specialShrine(const ActorContext &actor,Object &object) {
    const auto *player=ports_.players.find(actor.player);const auto &rule=*object.rule.shrine;
    if(rule.code==17) {
        auto plan=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::Shrine,object.random,player->position+Vec{5,5});
        if(!plan) return {plan.status,{}};
        ports_.travel.commitSpecialPortal(std::move(*plan.value));return {DomainStatus::Applied,std::monostate{}};
    }
    if(rule.code==20) {
        const auto &area=ports_.areas.at(object.area);EntityId selected;float distance=std::numeric_limits<float>::max();
        for(const auto &[id,m]:ports_.monsters.read().actors) {
            if(m.area!=object.area || !m.enemyTarget() || m.life<=0 || m.identity.rank!=MonsterRank::Normal || !m.rule.shrineUpgrade || m.moving || m.busyUntil>actor.tick || !area.definition.activation.nearby(object.position,m.position)) continue;
            const auto delta=m.position-player->position;const auto candidate=delta.x*delta.x+delta.y*delta.y;
            if(candidate<distance) {selected=id;distance=candidate;}
        }
        if(selected) return ports_.monsters.upgradeShrine(selected,actor.tick);
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(rule.code==19) return shrineMissiles(actor,object);
    if(rule.code!=18 && rule.code!=21 && rule.code!=22) return {};
    loot::ObjectSource output{object.definition,object.rule.operation,{},{}};
    auto random=object.random;
    if(rule.code==18) {
        for(const auto &[id,item]:player->persistent.inventory.items) {
            (void)id;const auto *at=std::get_if<ContainerLocation>(&item.location);const auto *definition=player->rules.items->find(item.definition);
            if(!at || at->container!=player->persistent.containers.backpack || !definition || definition->betterGem.empty() || definition->betterGem=="non") continue;
            output.ingredient=item.handle();output.dropCode=definition->betterGem;break;
        }
        if(output.dropCode.empty()) {constexpr const char *codes[]{"gcw","gcr","gcg","gcb","gcy","gcv"};output.dropCode=codes[limitedRandom(random,6)];}
        output.dropCount=1;
    } else {
        if(rule.argument0<0 || rule.argument1<=rule.argument0) return {DomainStatus::Unavailable,{}};
        output.dropCode=rule.code==21?"opm":"gpm";output.dropCount=rule.argument0+int(limitedRandom(random,unsigned(rule.argument1-rule.argument0)));
    }
    output.dropLevel=player->persistent.player.level;
    LootRequest source;source.source=object.id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
    const auto queued=ports_.loot.queue({object.revision,source,actor.player,player->position,std::move(output),{}});
    if(queued) {object.pending=true;object.operatorId=actor.player;object.random=random;}
    return queued;
}
}
