#include "system.hpp"
#include "timed_treasure.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <bit>

namespace d2x::server::maps {
DomainResult<> System::advanceAncientsDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    if(death.phase==Phase::Counter) {
        if((state_.eventProgress&0x110)!=0x10) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
        state_.eventProgress|=0x20u<<(unit.identity.nativeClass-989);
        const int selected=std::popcount(state_.eventProgress&0xe),dead=std::popcount(state_.eventProgress&0xe0);
        if(selected>dead) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
        state_.eventProgress|=0x100;death.eventChoice=dead;
        const auto altar=ports_.objects.read().objects.find(state_.bindings[0].id);
        if(altar!=ports_.objects.read().objects.end() && altar->second.definition==592) {
            const int count=int(pd2CrtRandom(ports_.random)%uint32_t(dead*6+4))+dead*12+8;
            const auto &treasures=state_.instance->catalog->relativeTreasures;
            if(treasures.size()<=950) return {DomainStatus::NotImplemented,{}};
            const auto id=altar->first;
            State::TimedTreasure job{treasures[950],count,false,altar->second.area,altar->second.position,true};
            state_.objectTreasures.emplace(std::pair{id,unit.deathOccurrence},job);
            ports_.world.retainControllerUnitRoom(id,job.area,job.position,true,pd2ObjectTreasureTimerHandler);
            auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Object,id},FrameEventType::MonsterModifier,
                tick.tick+1,false,pd2ObjectTreasureTimerHandler,count,0,unit.deathOccurrence});
            ports_.frameEvents.publish(std::move(timer));
            death.remaining=dead;death.phase=Phase::Reward;
        } else death.phase=Phase::Portal;
    }
    while(death.phase==Phase::Reward || death.phase==Phase::Wait) {
        if(death.phase==Phase::Reward) {
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest actor;actor.source=unit.id;actor.identity=unit.identity;actor.region=unit.area;actor.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,actor,{},unit.position};request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.code="std";drop.level=130;drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;death.phase=Phase::Wait;
        }
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        --death.remaining;death.rewardPositionPrepared=false;death.rewardPosition.reset();
        death.phase=death.remaining>0?Phase::Reward:Phase::Portal;
    }
    if(death.phase==Phase::Portal) {
        if(!death.evaluated) {
            const auto &area=ports_.areas.at(RegionId(168)).definition;
            const auto point=Vec{0x4e4d,0x3705}-area.origin;
            if(const auto room=area.activation.room(point)) {
                (void)room;
                if(const auto free=ports_.world.nativeFreeSpace(RegionId(168),point,point,1)) death.portalPosition=free->position;
            }
            death.evaluated=true;
        }
        if(death.portalPosition) {
            auto portal=ports_.travel.prepareNativePermanentPortal(RegionId(168),*death.portalPosition,109,tick.tick,586);
            if(isDeferred(portal.status)) return {portal.status,{}};
            if(portal) {
                const auto &link=portal.value->next.rbegin()->second;
                ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);ports_.world.retainControllerRoom(link.town,link.townPosition,true);
                ports_.travel.commitSpecialPortal(std::move(*portal.value));
            }
            death.phase=Phase::Cleanup;
        } else death.phase=Phase::Complete;
    }
    if(death.phase==Phase::Cleanup) {
        if(death.eventChoice==3) if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const auto counted=ports_.transactions.incrementPd2Counter(
                {killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick},7);
            if(isDeferred(counted.status)) return counted;
        }
        death.phase=Phase::Complete;
    }
    if(death.phase==Phase::Complete && (state_.eventProgress&0x100)) for(size_t slot=0;slot<4;++slot) {
        const auto &binding=state_.bindings[slot];
        ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
