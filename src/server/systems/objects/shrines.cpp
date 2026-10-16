#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
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
    missiles::MonsterSpawn request{object.id,object.area,area.generation,actor.tick,object.position,object.position,attack,{},player->persistent.player.level,0,0};
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
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==object.area) add(p.actor,p.position,int64_t(p.persistent.player.hp.raw()));}
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
    ports_.combat.commitSpells(std::move(*plan.value));
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
            if(m.area!=object.area || !m.enemyTarget() || m.life<=0 || m.identity.rank!=MonsterRank::Normal || !m.rule.shrineEligible || !m.rule.modifierChoices ||
                (m.moving && m.running) || m.busyUntil>actor.tick || !area.definition.activation.nearby(object.position,m.position)) continue;
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
DomainResult<> System::shrine(const ActorContext &actor,Object &object) {
    const auto &rule=*object.rule.shrine;const auto *player=ports_.players.find(actor.player);
    const auto &attributes=player->totals.character;
    if(rule.code>=17) return specialShrine(actor,object);
    if(rule.code<=3) return ports_.transactions.resources(actor,player->characterRevision,
        rule.code!=3?attributes.maxLifeFixed:player->persistent.player.hp,
        rule.code!=2?attributes.maxManaFixed:player->persistent.player.mana,player->persistent.player.stamina);
    if(rule.code<6 || rule.code>15 || rule.state.id<0) return {};
    const auto duration=curseStateDuration(rule.duration,attributes.combat.curseResistance,rule.state.curse);
    if(!duration) return {DomainStatus::Conflict,{}};
    const auto &stats=*player->rules.character->stats;
    CombatEffectSpec effect;effect.state=rule.state;effect.source={CombatEffectSource::Shrine,object.id,0,0};
    effect.stacking=EffectStacking::CurseLevel;effect.nativeState=true;effect.refreshPassivesOnRemoval=true;
    if(*duration) effect.duration=uint32_t(*duration);
    // sub_6FD10EC0 seeds the primary stat before posting. Reusing the same
    // shrine list preserves that initial value; the caller still writes its
    // additional stats afterwards, exactly like CombatBoost/Stamina.
    StatList initial;initial.extended=false;
    const auto primary=[&](std::string_view name,int32_t value) {setNativeStat(initial,stats,name,value,0,true);};
    const auto write=[&](std::string_view name,int32_t value) {return StatValue{{stats.names.at(std::string(name)),0},value};};
    std::vector<StatValue> writes;
    switch(rule.code) {
    case 6: primary("skill_armor_percent",rule.argument0);break;
    case 7: {
        const auto &weapon=player->totals.equipment.weapons[0];
        const auto rate=weapon.potion?0:weapon.baseAttackRating;
        const auto toHit=player->rules.character->formulas->skills.at(0).number("tohit");
        const auto percent=nativeAdd(weapon.attackRatingPercent,toHit);
        const auto bonus=nativeAdd(nativeMultiply(rate,nativeAdd(nativeDivide(percent,100),1)),player->definition.toHitFactor);
        primary("damagepercent",rule.argument1);
        writes.push_back(write("tohit",nativeMultiplyDivide(rule.argument0,bonus,100)));
        break;
    }
    case 8: primary("fireresist",rule.argument0);effect.modifiers.combat.preventBurn=true;break;
    case 9: primary("coldresist",rule.argument0);break;
    case 10: primary("lightresist",rule.argument0);break;
    case 11: primary("poisonresist",rule.argument0);effect.modifiers.combat.preventPoison=true;break;
    case 12:
        primary("maxstamina",0);effect.nativeRemoval=NativeStateRemoval::SkillShrine;break;
    case 13: primary("manarecoverybonus",rule.argument0);break;
    case 14: {
        const auto value=nativeMultiplyDivide(rule.argument0,nativeStatValue(player->totals.nativeStats,stats,"skill_staminapercent"),100);
        primary("skill_staminapercent",value);
        writes.push_back(write("stamina",nativeMultiply(2,value)));
        writes.push_back(write("staminarecoverybonus",1000));
        effect.restoreStaminaOnRemoval=true;break;
    }
    case 15: primary("item_addexperience",rule.argument0);break;
    }
    effect.nativeValues=std::move(initial.base);
    effect.modifiers=nativeCharacterModifiers(StatSnapshot{*effect.nativeValues},stats,effect.modifiers);
    return ports_.effects.apply(actor,std::move(effect),rule.code==14,writes,rule.code==12);
}

}
