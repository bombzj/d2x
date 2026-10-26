#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"

namespace d2x::server::maps {
DomainResult<> System::advanceEndgameDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    if(state_.instance->controller==44) return advanceAncientsDeath(unit,death,tick);
    if(state_.instance->controller!=41) return advanceBossSceneDeath(unit,death,tick);
    if(death.phase==Phase::Counter) {
        const auto bit=unit.identity.nativeClass==704?0x10u:unit.identity.nativeClass==705?0x20u:0x40u;
        state_.eventProgress|=bit;
        death.phase=(state_.eventProgress&0x70)==0x70?Phase::Reward:Phase::Complete;
    }
    while(death.phase==Phase::Reward || death.phase==Phase::Wait) {
        if(death.phase==Phase::Reward) {
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.code=death.eventStep==0?"cm2":"dcho";drop.level=130;
            drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;death.phase=Phase::Wait;
        }
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        ++death.eventStep;death.rewardPositionPrepared=false;death.rewardPosition.reset();
        death.phase=death.eventStep<2?Phase::Reward:Phase::Cleanup;
    }
    if(death.phase==Phase::Cleanup) {
        if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const auto counted=ports_.transactions.incrementPd2Counter(
                {killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick},6);
            if(isDeferred(counted.status)) return counted;
        }
        death.phase=Phase::Portal;
    }
    if(death.phase==Phase::Portal) {
        if(!death.evaluated) {
            death.evaluated=true;
            if(const auto free=ports_.world.nativeFreeSpace(unit.area,unit.position,unit.position,1)) death.portalPosition=free->position;
        }
        if(!death.portalPosition) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
        auto portal=ports_.travel.prepareNativePermanentPortal(unit.area,*death.portalPosition,109,tick.tick,60);
        if(isDeferred(portal.status)) return {portal.status,{}};
        if(portal) {
            const auto &link=portal.value->next.rbegin()->second;
            ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);
            ports_.world.retainControllerRoom(link.town,link.townPosition,true);
            ports_.travel.commitSpecialPortal(std::move(*portal.value));
        }
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
