#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/maps/pd2/event_program.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::mendelnWave(EntityId id,TickContext tick) {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !state_.instance || state_.instance->controller!=11) return {DomainStatus::InvalidActor,{}};
    auto &wave=state_.mendelnWaves[id];
    if(!wave.planned) {
        const auto &parameters=unit->rule.ai.pd2Parameters;
        const auto draw=[&](int value,int minimum) {return value<minimum+1?minimum:int(pd2CrtRandom(ports_.random)%uint32_t(value));};
        // 35BDD0 consumes the first group's three draws even when a later
        // wave overwrites those counts with the next fixed-column group.
        wave.counts={draw(parameters[1][0],2),draw(parameters[1][1],0),draw(parameters[1][2],0)};
        const int iteration=int(state_.eventProgress)-1;
        const int first=parameters[0][0]+1,second=first+parameters[0][1];
        if(iteration>=first && iteration<second)
            wave.counts={draw(parameters[2][0],1),draw(parameters[2][1],1),draw(parameters[2][2],0)};
        else if(iteration>=second)
            wave.counts={draw(parameters[3][0],1),draw(parameters[3][1],1),draw(parameters[3][2],1)};
        if(wave.counts[0]+wave.counts[1]+wave.counts[2]<=0) ++wave.counts[0];
        wave.planned=true;ports_.world.retainControllerUnitRoom(id,unit->area,unit->position,true);
    }
    while(wave.group<3) {
        if(wave.index>=wave.counts[size_t(wave.group)]) {++wave.group;wave.index=0;continue;}
        if(!wave.positionPrepared) {
            const int x=int(pd2CrtRandom(ports_.random)%20)-10,y=int(pd2CrtRandom(ports_.random)%20)-10;
            wave.position=prepareScenePosition(unit->area,unit->position+Vec{float(x),float(y)},2,4,false,0x101);
            wave.positionPrepared=true;
            if(wave.position) {
                const auto pool=pd2::mendelnPool();const auto fallback=pd2::mendelnPool(true);
                wave.definition=pool[pd2CrtRandom(ports_.random)%pool.size()];
                if(wave.definition<0) wave.definition=fallback[pd2CrtRandom(ports_.random)%fallback.size()];
            }
        }
        if(wave.position) {
            const auto &point=*wave.position;ports_.world.activate(point.area,point.position);
            if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
            const auto rank=wave.group==0?MonsterRank::Normal:wave.group==1?MonsterRank::Champion:MonsterRank::Unique;
            const auto born=ports_.monsters.advancePd2ScenePack(wave.pack,point.area,point.position,wave.definition,rank,
                "map-mendeln."+std::to_string(id.value)+"."+std::to_string(state_.eventProgress)+"."+
                std::to_string(wave.group)+"."+std::to_string(wave.index),tick.tick,ports_.spatial.nativeCollisionSample());
            if(!born) return born;
        }
        ++wave.index;wave.positionPrepared=false;wave.position.reset();wave.pack={};
    }
    ++state_.eventProgress;ports_.world.retainControllerUnitRoom(id,unit->area,unit->position,false);
    state_.mendelnWaves.erase(id);return {DomainStatus::Applied,std::monostate{}};
}
}
