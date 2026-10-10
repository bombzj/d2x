#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace d2x::server::objects {
void System::initialize(Object &object,uint64_t tick) {
    object.random=initialRandom(uint32_t(object.id.value)^ports_.settings.mapSeed);
    if(object.rule.initialization==22 || object.rule.initialization==24) object.hazardAt=tick+25;
    if(object.rule.initialization==22 && !object.mode) object.mode=object.rule.initialMode;
    if(object.rule.initialization==34 && limitedRandom(object.random,2)) object.mode=1;
    if(object.rule.initialization==51 && (object.mode==1 || object.mode==2)) object.hazardAt=tick+35;
    if(object.rule.initialization==58) object.hazardAt=tick+25+limitedRandom(object.random,250);
}
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area=ports_.areas.find(request.area);
    if(!area || !std::isfinite(request.position.x) || !std::isfinite(request.position.y) || request.mode<0 || request.mode>7) return {DomainStatus::InvalidRequest,{}};
    const auto source=area->definition.objectRules.find(request.definition);
    if(source==area->definition.objectRules.end() || (request.position.x<0 || request.position.y<0 || request.position.x>=area->definition.collision.width || request.position.y>=area->definition.collision.height)) return {DomainStatus::InvalidRequest,{}};
    auto rule=source->second;
    if(request.shrine) {
        const int code=*request.shrine==4?2:*request.shrine==5?3:*request.shrine==16?18:*request.shrine;
        const auto shrine=area->definition.shrineRules.find(code);
        if(rule.operation!=2 || shrine==area->definition.shrineRules.end() || *request.shrine<1) return {DomainStatus::InvalidRequest,{}};
        rule.shrine=shrine->second;
    }
    if(request.trap) {
        if(*request.trap<0 || *request.trap>9 || rule.operation!=4) return {DomainStatus::InvalidRequest,{}};
        rule.chest.emplace();rule.chest->trap=*request.trap;
    }
    if(state_.objects.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    const auto id=ports_.ids.allocate();Object object{id,request.definition,request.area,request.position,1,request.mode,std::move(rule)};
    object.uses=2*object.rule.parameters[2];initialize(object,request.tick);state_.objects.emplace(id,std::move(object));collision(request.area);
    return {DomainStatus::Applied,id};
}
DomainResult<> System::damage(Object &object,DamageType type,bool playersOnly,int radius,TickContext tick,std::optional<EntityId> only) {
    const auto &area=ports_.areas.at(object.area);if(area.definition.town) return {DomainStatus::Applied,std::monostate{}};
    auto random=object.random;
    combat::SpellImpact impact{object.id,object.id,object.area,type,0,{}};impact.environment=true;impact.environmentGeneration=area.generation;impact.occurrence=object.revision*8+uint64_t(type);
    auto add=[&](EntityId id,Vec at,int64_t life,int level,int dexterity,int defense) {
        if(only && id!=*only) return;
        const int dx=int(at.x)-int(object.position.x),dy=int(at.y)-int(object.position.y);
        if(life<=0 || dx*dx+dy*dy>radius*radius || !area.definition.collision.interactionSegment(at,object.position,std::max(object.rule.width,object.rule.height),object.id)) return;
        const int parameter=2*(int(uint8_t(level+limitedRandom(random,unsigned(level>>2))))-5*(dexterity>>1)-level);
        if(int(limitedRandom(random,100))>=std::max(parameter-defense+125,65)) return;
        const auto minimum=std::max<int64_t>(life>>5,1),maximum=std::max(life>>3,minimum+1);
        const auto amount=(minimum+limitedRandom(random,unsigned(maximum-minimum+256)))*object.rule.damage/100;
        if(amount>0) {impact.targets.push_back(id);impact.targetDamage.push_back(amount);}
    };
    for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==object.area) add(p.actor,p.position,int64_t(p.persistent.player.hp*256.f),p.persistent.player.level,p.totals.character.dexterity,p.totals.character.defense);}
    if(!playersOnly) for(const auto &[id,m]:ports_.monsters.read().actors) if(m.area==object.area && m.damageable()) add(id,m.position,m.life,m.rule.level,m.hireling?m.hirelingDexterity:0,ports_.effects.unitDefense(id,tick.tick));
    const auto result=ports_.combat.enqueue(std::move(impact));if(result) object.random=random;return result;
}
DomainResult<> System::explode(Object &object,const ActorContext &actor) {
    // Plan the entire connected barrel chain before changing any mode or RNG.
    std::vector<EntityId> chain{object.id};std::set<EntityId> seen{object.id};
    if(object.rule.operation==7) for(size_t i=0;i<chain.size();++i) {
        const auto &current=state_.objects.at(chain[i]);
        for(const auto &[id,other]:state_.objects) {
            if(other.area!=object.area || other.definition!=11 || other.mode || other.pending || seen.contains(id)) continue;
            const int dx=int(current.position.x)-int(other.position.x),dy=int(current.position.y)-int(other.position.y);
            if(dx*dx+dy*dy<=4) {seen.insert(id);chain.push_back(id);}
        }
    }
    // Each barrel has independent native damage rolls. Capacity is checked for
    // all impacts up front so a retry cannot re-explode a committed predecessor.
    if(ports_.combat.read().spells.size()+chain.size()*2>256 || ports_.combat.read().spellTargets+chain.size()*(ports_.players.all().size()+ports_.monsters.read().actors.size())*2>65536) return {DomainStatus::Capacity,{}};
    for(const auto id:chain) {
        auto &current=state_.objects.at(id);
        auto result=damage(current,DamageType::Physical,false,3,{actor.tick},current.rule.operation==30?std::optional{actor.actor}:std::nullopt);if(!result) return result;
        if(current.rule.operation==30) {result=damage(current,DamageType::Fire,true,3,{actor.tick},actor.actor);if(!result) return result;}
        current.mode=1;current.until=actor.tick+std::max(uint64_t{1},current.rule.openingTicks);++current.revision;
    }
    collision(object.area);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::hazardStep(TickContext tick) {
    bool blocked=false;std::set<RegionId> changed;
    for(auto &[id,object]:state_.objects) {
        (void)id;if(!object.hazardAt || tick.tick<object.hazardAt) continue;
        const auto &area=ports_.areas.at(object.area);
        bool occupied=false,active=false;
        const int radius=object.rule.initialization==24?0:object.rule.parameters[0]+1;
        for(const auto &[key,p]:ports_.players.all()) {
            (void)key;if(!p.entered || p.area!=object.area || p.persistent.player.hp<=0) continue;
            if(area.definition.activation.nearby(p.position,object.position)) active=true;
            const int dx=int(p.position.x)-int(object.position.x),dy=int(p.position.y)-int(object.position.y);if(dx*dx+dy*dy<=radius*radius) occupied=true;
        }
        if(!active) continue;
        if(object.rule.initialization==51) {
            if(object.mode==1) object.hazardAt=tick.tick+10;
            else if(object.mode==2 && !object.phase) {
                if(occupied) {const auto result=damage(object,DamageType::Fire,true,radius,tick);if(!result) {blocked|=result.status==DomainStatus::Capacity;continue;}}
                object.phase=1;object.hazardAt=tick.tick+25;
            } else if(object.mode==2 && !object.pending) {
                const PlayerState *player=ports_.players.find(object.operatorId);
                if(!player) for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==object.area) {player=&p;break;}}
                if(!player) continue;
                LootRequest source;source.source=object.id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
                const auto result=ports_.loot.queue({object.revision,source,player->player,object.position,loot::ObjectSource{object.definition,48,{},{}},{}});
                if(result) {object.pending=true;object.hazardAt=0;}else blocked|=result.status==DomainStatus::Capacity;
            } else if(object.mode==3) {object.mode=4;++object.revision;changed.insert(object.area);object.hazardAt=0;}
            else object.hazardAt=0;
        } else if(object.rule.initialization==58) {
            object.mode=1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);++object.revision;changed.insert(object.area);object.hazardAt=tick.tick+25+limitedRandom(object.random,250);
        } else if(object.rule.initialization==22) {
            if(occupied) {const auto result=damage(object,DamageType::Fire,true,radius,tick);if(!result) {blocked|=result.status==DomainStatus::Capacity;continue;}}
            if(object.mode==1) {object.mode=2;++object.revision;changed.insert(object.area);}
            object.hazardAt=tick.tick+15+limitedRandom(object.random,35);
        } else if(object.rule.initialization==24) {
            if(object.phase==0) {
                if(occupied) {if(!ports_.events.hasCapacity(1)) {blocked=true;continue;}const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{object.id,2,object.area,13}}});if(!result) {blocked=true;continue;}}
                object.phase=occupied?1:0;object.hazardAt=tick.tick+(occupied?25:15);}
            else if(object.phase==1) {
                if(occupied) {const auto result=damage(object,DamageType::Physical,true,0,tick);if(!result) {blocked|=result.status==DomainStatus::Capacity;continue;}}
                object.phase=occupied?2:0;object.mode=occupied?1:0;++object.revision;changed.insert(object.area);object.hazardAt=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);
            } else {object.phase=0;object.mode=0;++object.revision;changed.insert(object.area);object.hazardAt=tick.tick+15;}
        }
    }
    for(const auto area:changed) collision(area);
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
