#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::maps {
DomainResult<> System::endTaintedSun(TickContext tick) {
    if(!state_.taintedEnding) {
        if(state_.eventProgress>=50) return {DomainStatus::Applied,std::monostate{}};
        state_.eventProgress=50;state_.taintedEnding=true;
    }
    auto &cleanup=state_.eventCleanup;
    if(!cleanup.evaluated) {
        const int act=ports_.areas.at(RegionId(state_.instance->destination.level)).definition.act;
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Game,{},{}},{EnvironmentFact{act,act,5,false}}});
        if(!sent) return {sent.status,{}};
        ports_.world.setActEclipse(act,false);cleanup.evaluated=true;
    }
    if(!cleanup.cleanupPrepared) {
        for(const auto &[id,unit]:ports_.monsters.read().actors)
            if(unit.alive() && unit.identity.nativeClass>=993 && unit.identity.nativeClass<=995) cleanup.cleanup.push_back(id);
        cleanup.cleanupPrepared=true;
    }
    while(cleanup.cleanupIndex<cleanup.cleanup.size()) {
        const auto killed=ports_.monsters.scriptDeath(cleanup.cleanup[cleanup.cleanupIndex],tick.tick,{},true);
        if(isDeferred(killed.status)) return killed;
        ++cleanup.cleanupIndex;
    }
    state_.taintedEnding=false;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::operateEvent(const ActorContext &actor,EntityId id) {
    synchronize();
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const auto object=ports_.objects.read().objects.find(id);
    if(!state_.instance || !player || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
        !area || area->generation!=actor.areaGeneration || object==ports_.objects.read().objects.end() || object->second.area!=actor.area)
        return {DomainStatus::InvalidRequest,{}};
    const int program=state_.instance->controller;
    if((program!=16 || state_.eventProgress!=1) && (program!=19 || (state_.eventProgress!=1 && state_.eventProgress!=3))) {
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{SoundFact{actor.actor,0,actor.area,19}}});
        return {sent?DomainStatus::Unavailable:sent.status,{}};
    }
    auto &operation=state_.eventOperations[id];
    if(operation.active) return {DomainStatus::Applied,std::monostate{}};
    if(operation.stage!=state_.eventProgress) operation={};
    operation.stage=state_.eventProgress;operation.active=true;
    const auto result=advanceEventOperation(id,{actor.tick});
    return isDeferred(result.status)?DomainResult<>{DomainStatus::Applied,std::monostate{}}:result;
}
DomainResult<> System::advanceEventOperation(EntityId id,TickContext tick) {
    if(state_.instance && state_.instance->controller==44) return advanceAncientsOperation(id,tick);
    auto &operation=state_.eventOperations.at(id);
    const auto source=ports_.objects.read().objects.find(id);
    if(source==ports_.objects.read().objects.end()) {operation.active=false;return {DomainStatus::Applied,std::monostate{}};}
    const auto &object=source->second;const int program=state_.instance->controller;
    if(program==19 && operation.stage==3)
        for(const auto &[victim,death]:state_.eventDeaths)
            if(const auto *unit=ports_.monsters.find(victim);unit && unit->identity.nativeClass==1064 && death.phase!=Death::Phase::Complete)
                return {DomainStatus::PendingContent,{}};
    if(operation.phase==0) {
        const auto mode=ports_.objects.setNativeMode(id,operation.stage==3?3:1,tick.tick,operation.stage!=3);
        if(!mode) return mode;
        operation.phase=1;
    }
    if(program==16) {
        const int act=ports_.areas.at(object.area).definition.act;
        const auto sent=ports_.events.publishGroup({
            {0,tick.tick,{}, {AudienceKind::Game,{},{}},{EnvironmentFact{act,act,5,true}}},
            {0,tick.tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{id,2,object.area,0xa78,true},SoundFact{id,2,object.area,0x24,true}}}});
        if(!sent) return {sent.status,{}};
        ports_.world.setActEclipse(act,true);
        state_.eventProgress=2;operation.active=false;operation.phase=2;
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(operation.stage==1) {
        if(operation.phase==2) {
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},
                {SoundFact{id,2,object.area,0x29c,true}}});
            if(!sent) return {sent.status,{}};
            operation.active=false;return {DomainStatus::Applied,std::monostate{}};
        }
        while(operation.attempt<40) {
            if(!operation.positionPrepared) {
                const int x=10+int(pd2CrtRandom(ports_.random)%20),y=10+int(pd2CrtRandom(ports_.random)%20);
                operation.position=prepareScenePosition(object.area,object.position+Vec{float(x),float(y)},2);
                operation.positionPrepared=true;
            }
            if(operation.position) {
                const auto &point=*operation.position;
                ports_.world.activate(point.area,point.position);
                if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
                const auto born=ports_.monsters.admitPd2SceneMonster(point.area,point.position,1064,
                    "map-event.guardian."+std::to_string(id.value),tick.tick);
                if(isDeferred(born.status)) return {born.status,{}};
                if(born) {
                    operation.monster=*born.value;state_.eventProgress=2;operation.phase=2;
                    return advanceEventOperation(id,tick);
                }
                if(born.status==DomainStatus::NotImplemented) return {born.status,{}};
            }
            ++operation.attempt;operation.positionPrepared=false;operation.position.reset();
        }
        operation.active=false;operation.phase=0;operation.attempt=0;
        return {DomainStatus::Unavailable,{}};
    }
    if(operation.remaining<0) {
        auto random=object.random;
        operation.remaining=2+int(pd2UnitEventRandom(random)&1);
        ports_.objects.commitLootRandom(id,object.random,random);
    }
    while(operation.remaining>0) {
        if(operation.phase==1) {
            if(!operation.reward.rewardPositionPrepared) prepareDropPosition(object.area,object.position,operation.reward);
            LootRequest source;source.source=id;source.region=object.area;
            source.difficulty=ports_.monsters.gameSettings().difficulty;
            loot::Request request{uint64_t(operation.remaining),source,{},object.position};
            request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.code="iwss";drop.quality=ItemQuality::Normal;drop.level=90;
            drop.exactPosition=true;drop.placementValid=operation.reward.rewardPosition.has_value();
            if(operation.reward.rewardPosition) {request.source.region=operation.reward.rewardPosition->area;request.position=operation.reward.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;
            operation.phase=2;
        }
        if(!ports_.loot.takeCompletion(id)) return {DomainStatus::PendingContent,{}};
        --operation.remaining;operation.phase=1;operation.reward={};
    }
    const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},
        {SoundFact{id,2,object.area,0x23,true}}});
    if(!sent) return {sent.status,{}};
    state_.eventProgress=4;operation.active=false;operation.phase=3;
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::advanceEventOperations(TickContext tick) {
    bool pending{};
    for(const auto &[id,operation]:state_.eventOperations) if(operation.active) pending|=!advanceEventOperation(id,tick);
    if(state_.taintedEnding) pending|=!endTaintedSun(tick);
    return pending;
}
}
