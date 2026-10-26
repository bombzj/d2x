#include "system.hpp"
#include "timed_treasure.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include <algorithm>

namespace d2x::server::maps {
void System::synchronizeTimedTreasures(TickContext tick) {
    for(auto it=state_.timedTreasures.begin();it!=state_.timedTreasures.end();) {
        const auto id=it->first.first;
        const auto *unit=ports_.monsters.find(id);
        auto &job=it->second;
        if(unit) {
            if(job.remaining>0 && !job.retained) {
                job.area=unit->area;job.position=unit->position;job.retained=true;
                ports_.world.retainControllerUnitRoom(id,job.area,job.position,true,pd2TreasureTimerHandler);
            }
            ++it;continue;
        }
        if(job.retained) ports_.world.retainControllerUnitRoom(id,job.area,job.position,false,pd2TreasureTimerHandler);
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,pd2TreasureTimerHandler);
        it=state_.timedTreasures.erase(it);
    }
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        if(unit.alive() || !unit.rule.stats || !unit.rule.stats->pd2) continue;
        const auto *area=ports_.areas.find(unit.area);
        const auto catalog=area && area->definition.pd2Map?area->definition.pd2Map->catalog:nullptr;
        if(!catalog) continue;
        const auto treasure=catalog->timedTreasures.find(unit.identity.nativeClass);
        if(treasure==catalog->timedTreasures.end()) continue;
        if(pd2::mapBossGroup(unit.identity.nativeClass) && !deathReady(unit)) continue;
        const auto key=std::pair{id,unit.deathOccurrence};
        if(state_.timedTreasures.contains(key)) continue;
        // 41d49 378D10 schedules the first native event7 on the next frame.
        auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,
            tick.tick+1,false,pd2TreasureTimerHandler,treasure->second.count,0,unit.deathOccurrence});
        state_.timedTreasures.emplace(key,State::TimedTreasure{treasure->second.treasure,treasure->second.count});
        auto &job=state_.timedTreasures.at(key);job.area=unit.area;job.position=unit.position;job.retained=true;
        ports_.world.retainControllerUnitRoom(id,job.area,job.position,true,pd2TreasureTimerHandler);
        ports_.frameEvents.publish(std::move(timer));
    }
}
DomainResult<> System::treasureEvent(const FrameEvent &event,TickContext tick) {
    const auto id=event.owner.unit;
    const auto found=state_.timedTreasures.find({id,event.payload});
    const auto *unit=ports_.monsters.find(id);
    if(!unit || found==state_.timedTreasures.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &job=found->second;
    if(job.remaining<1 || job.remaining>=2000) {
        if(job.retained) ports_.world.retainControllerUnitRoom(id,job.area,job.position,false,pd2TreasureTimerHandler);
        job.retained=false;return {DomainStatus::Applied,std::monostate{}};
    }
    if(!job.waiting) {
        // 37FA90 -> 3A1AF0 -> Game E5D0 reads monster STAT_LEVEL and
        // clamps its result to at least one, independently of map level.
        const int level=std::max(1,unit->nativeStats?nativeStatValue(unit->nativeStats->values,*unit->rule.stats,"level"):unit->rule.level);
        LootRequest source;source.source=id;source.identity=unit->identity;source.region=unit->area;
        source.difficulty=unit->rule.difficulty;source.sourceSeed=true;source.monsterPlayerCount=unit->admittedPlayerCount;
        loot::Request request{event.payload,source,{},unit->position};request.environment=true;request.reportCompletion=true;
        request.nativeTreasure=loot::NativeTreasure{job.treasure,level};
        const auto queued=ports_.loot.queue(request);
        if(!queued) return queued;
        job.waiting=true;
    }
    const auto completed=ports_.loot.takeCompletionDetail(id);
    if(!completed) return {DomainStatus::PendingContent,{}};
    job.remaining-=int(std::min(completed->created,size_t(INT32_MAX)));
    job.waiting=false;
    if(job.remaining>0) {
        // Count successful creations, not TC calls. NoDrop still waits five
        // native frames; it is never expanded into a same-frame retry loop.
        auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,
            tick.tick+5,false,pd2TreasureTimerHandler,job.remaining,0,event.payload});
        ports_.frameEvents.publish(std::move(timer));
    } else {
        ports_.world.retainControllerUnitRoom(id,job.area,job.position,false,pd2TreasureTimerHandler);job.retained=false;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
