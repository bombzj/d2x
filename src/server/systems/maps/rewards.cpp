#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::maps {
DomainResult<> System::advanceImperialReward(const monsters::Actor &unit,Death &death,TickContext) {
    using Phase=Death::Phase;
    if(death.phase==Phase::Reward) {
        if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
        loot::NativeDrop drop;drop.code=unit.identity.nativeClass==1148?"lpp":"lbox";drop.level=130;
        drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
        if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
        request.nativeDrops.push_back(std::move(drop));
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;death.phase=Phase::Wait;
    }
    if(death.phase==Phase::Wait) {
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::advanceCharmReward(const monsters::Actor &unit,Death &death,TickContext) {
    using Phase=Death::Phase;
    if(death.phase==Phase::Reward) {
        if(!death.reward) death.reward=size_t(pd2::sceneCharmPrefixes[pd2CrtRandom(ports_.random)%pd2::sceneCharmPrefixes.size()]);
        if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
        loot::NativeDrop drop;drop.code="cm3";drop.quality=ItemQuality::Magic;drop.level=unit.identity.nativeClass==1058?99:90;
        drop.preferredPrefix=int(*death.reward);drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
        if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
        request.nativeDrops.push_back(std::move(drop));
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;death.phase=Phase::Wait;
    }
    if(death.phase==Phase::Wait) {
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::advanceRandomRewards(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    const bool socketScene=state_.instance->controller==35;
    if(death.phase==Phase::Reward) {
        if(death.remaining<0) {
            if(!ports_.areas.hasRoomUnit(unit.id)) {
                death.suppressTreasure=true;death.phase=Phase::Complete;
                return {DomainStatus::Applied,std::monostate{}};
            }
            death.remaining=socketScene?3:2+int(pd2CrtRandom(ports_.random)&1);
        }
        if(!death.reward) {
            const auto &pool=state_.instance->catalog->sceneUniques;
            // 31 has one initial candidate and 19 retries. 35 decrements
            // its initial 40 before drawing, so it has at most 39 draws.
            const int attempts=socketScene?39:20;
            for(int attempt=0;attempt<attempts && !pool.empty();++attempt) {
                const size_t row=pd2CrtRandom(ports_.random)%pool.size();
                if(pool[row].eligible && (!socketScene || pool[row].socketable)) {death.reward=row;break;}
            }
            if(!death.reward) {
                death.suppressTreasure=true;death.phase=Phase::Complete;
                return {DomainStatus::Applied,std::monostate{}};
            }
        }
        if(!death.rewardPositionPrepared) prepareDropPosition(unit,death,true);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
        loot::NativeDrop drop;drop.uniqueRow=death.reward;drop.resetUnique=true;
        drop.uniqueSelection=loot::NativeDrop::UniqueSelection::Base;
        drop.adjustment=socketScene?loot::NativeDrop::Adjustment::RandomSockets:loot::NativeDrop::Adjustment::Corruption;
        drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
        if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
        request.nativeDrops.push_back(std::move(drop));
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;
        death.phase=Phase::Wait;
    }
    if(death.phase==Phase::Wait) {
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        --death.remaining;death.reward.reset();death.rewardPosition.reset();death.rewardPositionPrepared=false;
        death.phase=death.remaining?Phase::Reward:socketScene?Phase::Complete:Phase::Cleanup;
        if(death.phase==Phase::Reward) return {DomainStatus::PendingContent,{}};
    }
    if(death.phase==Phase::Cleanup) {
        if(!death.cleanupPrepared) {
            const auto &area=ports_.areas.at(unit.area).definition;
            if(const auto *room=area.activation.room(unit.position))
                for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data())))
                    for(const auto id:ports_.areas.roomUnitOrder(near))
                        if(const auto *other=ports_.monsters.find(id);other && other->identity.nativeClass==1057 && other->alive())
                            death.cleanup.push_back(id);
            death.cleanupPrepared=true;
        }
        while(death.cleanupIndex<death.cleanup.size()) {
            const auto killed=ports_.monsters.scriptDeath(death.cleanup[death.cleanupIndex],tick.tick,{},true);
            if(isDeferred(killed.status)) return killed;
            ++death.cleanupIndex;
        }
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
