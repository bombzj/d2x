#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/maps/pd2/event_program.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::invaderWave(EntityId id,TickContext tick) {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !state_.instance || state_.instance->controller!=20) return {DomainStatus::InvalidActor,{}};
    auto &birth=state_.invaderBirths[id];
    if(birth.phase==4) {
        const auto killed=ports_.monsters.scriptDeath(id,tick.tick,{},true);if(!killed) return killed;
        state_.invaderBirths.erase(id);return {DomainStatus::Applied,std::monostate{}};
    }
    const auto progress=state_.eventProgress;
    const auto spawned=(progress>>16)&255,wave=(progress>>8)&255;
    if(!birth.phase && (spawned>4 || wave>2 || (progress&255)>3))
        return ports_.monsters.scriptDeath(id,tick.tick,{},true);
    if(!birth.phase) {
        std::array<int,7> classes{1183,1184,1185,1186,1187,1188,1189};
        pd2::shuffleMapEvent(classes,state_.instance->initSeed);
        birth.selection=classes[spawned]-1183;
        if(!birth.positionPrepared) {
            const auto free=ports_.world.nativeFreeSpace(unit->area,unit->position,unit->position,3);
            if(free) birth.position=PointTarget{free->area,ports_.areas.at(free->area).generation,free->position};
            if(!birth.position) birth.position=PointTarget{unit->area,ports_.areas.at(unit->area).generation,unit->position};
            birth.positionPrepared=true;
        }
        if(birth.position) {
            const auto &point=*birth.position;
            ports_.world.activate(point.area,point.position);
            if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
            const auto born=ports_.monsters.admitPd2NativeChild(id,point.area,point.position,1183+birth.selection,1,0,false,
                tick.tick,ports_.spatial.nativeCollisionSample());
            if(isDeferred(born.status) || born.status==DomainStatus::NotImplemented) return {born.status,{}};
            if(born && *born.value) {birth.child=*born.value;state_.eventProgress+=0x10000;birth.phase=1;}
        }
        if(!birth.child) {state_.invaderBirths.erase(id);return {DomainStatus::Applied,std::monostate{}};}
    }
    if(birth.phase==1) {
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},unit->area},
            {SoundFact{id,1,unit->area,16,true}}});
        if(!sent) return {sent.status,{}};
        birth.phase=2;
    }
    if(birth.phase==2) {
        constexpr std::array<std::string_view,7> keys{"MapEventNotifyInvadersAmazon","MapEventNotifyInvadersAssassin",
            "MapEventNotifyInvadersBarbarian","MapEventNotifyInvadersDruid","MapEventNotifyInvadersNecromancer",
            "MapEventNotifyInvadersPaladin","MapEventNotifyInvadersSorceress"};
        const auto sent=notify(keys[size_t(birth.selection)],1,tick);if(!sent) return sent;
        birth.phase=3;
    }
    const auto marked=ports_.monsters.toggleAiState(birth.child,209,true,tick.tick);if(!marked) return marked;
    const auto total=(state_.eventProgress>>16)&255;
    // 35DE10 never resets the shared spawned byte between portals. Its
    // three completions occur at total counts one, three, and five.
    if(total>=(wave==0?1u:wave==1?3u:5u)) {
        state_.eventProgress=(state_.eventProgress&0xffff00ffu)|(((wave+1)&255)<<8);
        birth.phase=4;
        const auto killed=ports_.monsters.scriptDeath(id,tick.tick,{},true);if(!killed) return killed;
    }
    state_.invaderBirths.erase(id);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::eventTreasure(const monsters::Actor &unit,Death &death,int offset,TickContext) {
    const auto &treasures=state_.instance->catalog->relativeTreasures;
    if(offset<0 || size_t(offset)>=treasures.size()) return {DomainStatus::Applied,std::monostate{}};
    if(!death.eventWaiting) {
        const int level=std::max(1,unit.nativeStats?nativeStatValue(unit.nativeStats->values,*unit.rule.stats,"level"):unit.rule.level);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;
        source.difficulty=unit.rule.difficulty;source.sourceSeed=true;source.monsterPlayerCount=unit.admittedPlayerCount;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
        request.nativeTreasure=loot::NativeTreasure{treasures[size_t(offset)],level};
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;
        death.eventWaiting=true;
    }
    if(!ports_.loot.takeCompletionDetail(unit.id)) return {DomainStatus::PendingContent,{}};
    death.eventWaiting=false;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::advanceInvaderDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    // 41d49 3783A0: six common TC calls precede the class item and
    // class-specific calls. Later CRT choices follow the earlier drops.
    if(death.phase==Death::Phase::Counter) {
        const auto count=((state_.eventProgress>>24)+1)&255;
        state_.eventProgress=(state_.eventProgress&0xffffffu)|(count<<24);
        death.phase=Death::Phase::Reward;
    }
    constexpr std::array common{0x419,0x41b,0x41a,0x41c,0x41c,0x41d};
    while(death.eventStep<int(common.size())) {
        const auto dropped=eventTreasure(unit,death,common[size_t(death.eventStep)],tick);if(!dropped) return dropped;
        ++death.eventStep;
    }
    const int type=unit.identity.nativeClass-1183;
    if(death.eventStep==6) {
        if(!death.eventWaiting) {
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
            constexpr std::array codes{"ivez","ivea","iveb","ived","iven","ivep","ives"};
            loot::NativeDrop drop;drop.code=codes[size_t(type)];drop.quality=ItemQuality::Normal;
            drop.level=std::max(1,unit.nativeStats?nativeStatValue(unit.nativeStats->values,*unit.rule.stats,"level"):unit.rule.level);
            drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;
            death.eventWaiting=true;
        }
        if(!ports_.loot.takeCompletionDetail(unit.id)) return {DomainStatus::PendingContent,{}};
        death.eventWaiting=false;++death.eventStep;
    }
    const std::array<std::vector<int>,7> fixed{{{0x426,0x416},{0x41e,0x416,0x429,0x429},
        {0x42a,0x418},{0x436,0x416,0x437,0x41f},{0x41e,0x417,0x438,0x439},
        {0x41e,0x418,0x43d,0x43e},{0x426,0x417}}};
    const auto &calls=fixed[size_t(type)];
    while(death.eventStep<7+int(calls.size())) {
        const auto dropped=eventTreasure(unit,death,calls[size_t(death.eventStep-7)],tick);if(!dropped) return dropped;
        ++death.eventStep;
    }
    if(death.eventChoice<0) death.eventChoice=type==0 || type==2 || type==6?
        int(pd2CrtRandom(ports_.random)%uint32_t(type==2?3:type==6?2:4)):0;
    const std::array<std::vector<int>,7> tail{{
        death.eventChoice==1?std::vector<int>{0x420,0x421}:death.eventChoice==2?std::vector<int>{0x422,0x423}:
            death.eventChoice==3?std::vector<int>{0x425,0x41f}:std::vector<int>{0x424},
        {},death.eventChoice==1?std::vector<int>{0x42b,0x42b}:death.eventChoice==2?std::vector<int>{0x42f,0x42f}:std::vector<int>{0x435},
        {},{},{},death.eventChoice==1?std::vector<int>{0x43b,0x41f}:std::vector<int>{0x43a}}};
    const auto &extra=tail[size_t(type)];
    while(death.eventStep<7+int(calls.size()+extra.size())) {
        const auto dropped=eventTreasure(unit,death,extra[size_t(death.eventStep-7-int(calls.size()))],tick);if(!dropped) return dropped;
        ++death.eventStep;
    }
    death.phase=Death::Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
}
}
