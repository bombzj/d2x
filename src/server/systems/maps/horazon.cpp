#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <array>

namespace d2x::server::maps {
DomainResult<> System::advanceHorazonDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    if(death.phase==Phase::Counter) {
        if(state_.eventProgress!=1) {death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
        const auto &tiers=state_.instance->catalog->loot.tiers;
        int tier=-1;
        for(size_t index=0;index<tiers.size();++index) if(tiers[index].contains(int(unit.area))) tier=int(index);
        std::vector<int> candidates;
        if(tier>=0) {
            constexpr std::array<std::array<int,3>,3> counts{{{3,2,1},{1,3,2},{1,2,3}}};
            for(int index=0;index<3;++index)
                for(int count=0;count<counts[size_t(tier)][size_t(index)];++count) death.eventTreasures.push_back(54+index);
            const auto append=[&](int index){candidates.insert(candidates.end(),tiers[size_t(index)].begin(),tiers[size_t(index)].end());};
            if(tier==0) {append(1);append(2);}
            else if(tier==1) {append(2);append(0);}
            else append(0);
        }
        if(!candidates.empty()) {
            do death.eventDestination=candidates[pd2CrtRandom(ports_.random)%candidates.size()];
            while(death.eventDestination==int(unit.area));
        }
        death.phase=Phase::Portal;
    }
    if(death.phase==Phase::Portal) {
        if(death.eventDestination && !death.eventStep) {
            const auto appended=ports_.world.appendPd2MapGlobal(state_.instance,
                StatValue{StatKey{10,10},death.eventDestination});
            if(!appended) return appended;
            death.eventStep=1;
        }
        if(death.eventDestination && !death.portalPhase) {
            const auto requested=ports_.world.requestMapArea(RegionId(death.eventDestination),state_.instance);
            if(isDeferred(requested.status)) return {requested.status,{}};
            if(requested && !ports_.areas.find(RegionId(death.eventDestination))) return {DomainStatus::PendingContent,{}};
            if(requested) {
                auto portal=ports_.travel.prepareNativePermanentPortal(unit.area,unit.position,death.eventDestination,tick.tick,60);
                if(isDeferred(portal.status)) return {portal.status,{}};
                if(portal) {
                    const auto &link=portal.value->next.rbegin()->second;
                    death.portalObject=link.fieldId;
                    ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);
                    ports_.world.retainControllerRoom(link.town,link.townPosition,true);
                    ports_.travel.commitSpecialPortal(std::move(*portal.value));
                }
            }
            death.portalPhase=1;
        }
        if(death.portalObject) {
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},unit.area},
                {SoundFact{death.portalObject,2,unit.area,0x9fc,true}}});
            if(!sent) return {sent.status,{}};
        }
        if(death.eventDestination) ++state_.eventProgress;
        death.eventStep=0;death.phase=Phase::Reward;
    }
    if(death.phase==Phase::Reward) {
        if(death.eventChoice<0) {
            death.eventChoice=pd2CrtRandom(ports_.random)%10==0?1:2;
            if(death.eventChoice==1) {
                auto random=unit.combatRandom;
                constexpr std::array<size_t,8> maps{432,433,436,453,452,478,472,482};
                death.reward=maps[pd2UnitEventRandom(random)&7];
                ports_.monsters.commitCombatRandom(unit.id,random);
            }
        }
        if(death.eventChoice==1) {
            if(!death.eventWaiting) {
                if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
                LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
                loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
                loot::NativeDrop drop;drop.uniqueRow=death.reward;drop.level=131;drop.resetUnique=true;
                drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
                if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
                request.nativeDrops.push_back(std::move(drop));
                const auto queued=ports_.loot.queue(request);if(!queued) return queued;
                death.eventWaiting=true;
            }
            if(!ports_.loot.takeCompletionDetail(unit.id)) return {DomainStatus::PendingContent,{}};
            death.eventWaiting=false;death.eventChoice=2;
        }
        while(death.eventStep<int(death.eventTreasures.size())) {
            const auto dropped=eventTreasure(unit,death,death.eventTreasures[size_t(death.eventStep)],tick);if(!dropped) return dropped;
            ++death.eventStep;
        }
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
