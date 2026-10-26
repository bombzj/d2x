#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::maps {
DomainResult<> System::advanceTierBossRewards(const monsters::Actor &unit,Death &death,TickContext tick) {
    const int tier=pd2::mapBossGroup(unit.identity.nativeClass);
    if(tier<1 || tier>4) return {DomainStatus::Applied,std::monostate{}};
    if(death.tierRewardPhase==0) {
        if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const auto counted=ports_.transactions.incrementPd2Counter(
                {killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick},tier==4?15:14);
            if(isDeferred(counted.status)) return counted;
        }
        death.tierRewardPhase=1;
    }
    while(death.tierRewardPhase<5) {
        const bool second=death.tierRewardPhase>=3;
        if(second && tier==1) {death.tierRewardPhase=5;break;}
        if(death.tierRewardPhase&1) {
            if(!death.tierRewardSelected) {
                auto random=unit.combatRandom;
                const uint32_t denominator=second?(tier==4?6u:15u):tier==1?11u:tier==2?25u:tier==4?20u:15u;
                const bool selected=pd2UnitEventRandom(random)%denominator==0;
                ports_.monsters.commitCombatRandom(unit.id,random);
                if(!selected) {death.tierRewardPhase+=2;continue;}
                death.tierRewardSelected=true;
                death.rewardPositionPrepared=false;death.rewardPosition.reset();
            }
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest source;source.source=unit.id;source.identity=unit.identity;
            source.region=unit.area;source.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,source,{},unit.position};
            request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.code=second?"scrb":"dcbl";drop.level=130;
            drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;
            ++death.tierRewardPhase;
        }
        if(!(death.tierRewardPhase&1)) {
            if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
            death.tierRewardSelected=false;++death.tierRewardPhase;
        }
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
