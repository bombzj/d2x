#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "timed_treasure.hpp"
#include <algorithm>

namespace d2x::server::maps {
bool System::handlesEventDeath(const monsters::Actor &unit) const {
    if(!state_.instance) return false;
    switch(state_.instance->controller) {
    case 11:return unit.identity.nativeClass==922;
    case 12:return unit.identity.nativeClass==919;
    case 14:return unit.identity.nativeClass==921 || (!unit.rule.noTreasure && unit.pd2DeathEventState);
    case 16:return unit.identity.nativeClass>=993 && unit.identity.nativeClass<=995;
    case 19:return unit.identity.nativeClass==1064;
    case 20:return unit.identity.nativeClass>=1183 && unit.identity.nativeClass<=1189;
    default:return false;
    }
}
DomainResult<> System::advanceEventDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    const int program=state_.instance->controller;
    if(program==14 && unit.identity.nativeClass==921) {
        const auto key=std::pair{unit.id,unit.deathOccurrence};
        if(!state_.wandererBirths.contains(key)) {
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Game,{},{}},
                {SoundFact{unit.id,1,unit.area,0x2c1,true}}});
            if(!sent) return {sent.status,{}};
            auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Monster,unit.id},FrameEventType::MonsterModifier,
                tick.tick+85,false,pd2WandererTimerHandler,0,0,unit.deathOccurrence});
            auto &job=state_.wandererBirths[key];job.area=unit.area;job.source=unit.position;
            ports_.world.retainControllerUnitRoom(unit.id,unit.area,unit.position,true,pd2WandererTimerHandler);
            ports_.frameEvents.publish(std::move(timer));
        }
        death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
    }
    if(program==20) return advanceInvaderDeath(unit,death,tick);
    if(program==12) return advanceHorazonDeath(unit,death,tick);
    if(death.phase==Phase::Counter) {
        if(program==19) {
            if(state_.eventProgress!=2) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
            if(!ports_.areas.hasRoomUnit(unit.id)) {
                death.suppressTreasure=true;death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
            }
            state_.eventProgress=3;
            const auto &area=ports_.areas.at(unit.area).definition;
            if(const auto *room=area.activation.room(unit.position))
                for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data())))
                    for(const auto id:ports_.areas.roomUnitOrder(near))
                        if(const auto *other=ports_.monsters.find(id);other && other->identity.nativeClass==1065 && other->alive()) death.cleanup.push_back(id);
            death.phase=Phase::Cleanup;
        } else if(program==11) {
            death.remaining=1;death.phase=Phase::Reward;
        } else if(program==14) {
            auto random=unit.combatRandom;
            death.remaining=4+int(pd2UnitEventRandom(random)%7);
            ports_.monsters.commitCombatRandom(unit.id,random);death.phase=Phase::Reward;
        } else {
            if(state_.eventProgress<2 || state_.eventProgress>=50) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
            const auto &group=ports_.areas.at(unit.area).definition.populationRules.classes.at(unit.identity.nativeClass);
            const int count=std::max(1,group.minimum+group.maximum);
            auto random=unit.combatRandom;
            const bool selected=pd2UnitEventRandom(random)%2400<uint32_t(1000/count);
            ports_.monsters.commitCombatRandom(unit.id,random);
            if(!selected) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
            death.remaining=1;death.phase=Phase::Reward;
        }
    }
    if(death.phase==Phase::Cleanup) {
        if(!death.evaluated) {
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Game,{},{}},
                {SoundFact{unit.id,1,unit.area,0x26d,true}}});
            if(!sent) return {sent.status,{}};
            death.evaluated=true;
        }
        while(death.cleanupIndex<death.cleanup.size()) {
            const auto killed=ports_.monsters.scriptDeath(death.cleanup[death.cleanupIndex],tick.tick,{},true);
            if(isDeferred(killed.status)) return killed;
            ++death.cleanupIndex;
        }
        death.phase=Phase::Complete;
    }
    while(death.phase==Phase::Reward || death.phase==Phase::Wait) {
        if(death.phase==Phase::Reward) {
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.level=program==14 || program==11?131:130;
            drop.code=program==11?"rtmv":program==14?"wss":unit.identity.nativeClass==993?"pk2":unit.identity.nativeClass==995?"pk3":"pk1";
            drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;
            death.phase=Phase::Wait;
        }
        const auto completed=ports_.loot.takeCompletionDetail(unit.id);
        if(!completed) return {DomainStatus::PendingContent,{}};
        if(program==16 && completed->created) ++state_.eventProgress;
        --death.remaining;death.rewardPositionPrepared=false;death.rewardPosition.reset();
        death.phase=death.remaining>0?Phase::Reward:Phase::Complete;
    }
    if(program==11 && death.phase==Phase::Complete && !death.evaluated) {
        const int count=20+int(pd2CrtRandom(ports_.random)%5);
        const auto &treasures=state_.instance->catalog->relativeTreasures;
        if(treasures.size()>0x3b7) {
            auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Monster,unit.id},FrameEventType::MonsterModifier,
                tick.tick+1,false,pd2TreasureTimerHandler,count,0,unit.deathOccurrence});
            state_.timedTreasures.emplace(std::pair{unit.id,unit.deathOccurrence},State::TimedTreasure{treasures[0x3b7],count});
            ports_.frameEvents.publish(std::move(timer));
        }
        death.evaluated=true;
    }
    if(program==16 && state_.eventProgress>7 && state_.eventProgress<50) return endTaintedSun(tick);
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::advanceEventDeaths(TickContext tick) {
    std::vector<EntityId> order;
    for(const auto &[id,unit]:ports_.monsters.read().actors)
        if(!unit.alive() && !unit.rewardComplete && handlesEventDeath(unit)) order.push_back(id);
    std::stable_sort(order.begin(),order.end(),[&](EntityId a,EntityId b) {
        return ports_.monsters.find(a)->deathOccurrence<ports_.monsters.find(b)->deathOccurrence;
    });
    for(const auto id:order) {
        const auto &unit=*ports_.monsters.find(id);
        if(!controllerDeathReady(unit)) return true;
        auto [found,created]=state_.eventDeaths.try_emplace(id);
        if(created || found->second.occurrence!=unit.deathOccurrence) {found->second={};found->second.occurrence=unit.deathOccurrence;}
        if(found->second.phase==Death::Phase::Complete) continue;
        const auto advanced=advanceEventDeath(unit,found->second,tick);
        if(!advanced) return true;
    }
    return false;
}
}
