#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "gameplay/quest/catalog.hpp"
#include <stdexcept>
#include "gameplay/quest/acts/act_five_state.hpp"
namespace d2x::server::quests {
DomainResult<> System::baalFx(RegionId region,Vec position) {
    auto &state=state_.actFive;
    const bool active=(state_.initialFlags[questDefinition(QuestId::EveOfDestruction).nativeSlot]&0x8000)==0;
    if(active && !state.tyraelSpawned) {
        // 1.13c ACT5Q6_SpawnTyrael RVA 5c1f0 passes radius 100, not 1.10f's 19.
        if(const auto free=ports_.world.questFreePosition(region,position,position-Vec{5,5},5,100)) {
            const auto result=ports_.world.admitQuestNpc(free->area,"tyrael3",free->position,4);
            if(result.status==DomainStatus::Capacity) return {result.status,{}};
            state.tyraelSpawned=bool(result);
        }
    }
    ports_.world.retainControllerRoom(region,position,false);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::baalStep(TickContext tick) {
    bool blocked=false;auto &world=state_.actFive;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        (void)id;
        if(!world.baalSlain && int(m.area)==132 && m.identity.monster=="baalcrab" && !m.owner && m.identity.origin!=SpawnOrigin::Debug && m.identity.origin!=SpawnOrigin::Summoned && !m.alive()) {
            world.baalSlain=true;world.baalPosition=m.position;world.baalFxSource=m.id;
            captureGoal(QuestId::EveOfDestruction,5,m.area);
        }
    }
    if(world.baalSlain && !world.baalFxStarted) {
        const auto definition=ports_.missiles.nativeDefinition("baalfx control");
        if(!definition) throw std::logic_error("Missing original Baal FX control missile");
        if(const auto *source=ports_.monsters.find(world.baalFxSource)) {
            const auto &area=ports_.areas.at(source->area);
            const auto result=ports_.missiles.spawnNative({source->id,source->area,area.generation,tick.tick,
                world.baalPosition,world.baalPosition,*definition,0,1});
            if(result.status==DomainStatus::Capacity) blocked=true;
            else {
                world.baalFxStarted=true;
                if(result) ports_.world.retainControllerRoom(source->area,world.baalPosition,true);
            }
        } else world.baalFxStarted=true;
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || p.persistent.player.hp<=0) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        if(int(p.area)==131 && !world.throneDeparted) for(const auto &throne:area.definition.npcs) if(const auto *unit=ports_.monsters.find(throne.id);throne.rule.code=="baalthrone" && unit && unit->alive() && (p.position-unit->position).length()<64) {
            if(world.wavePrepared && world.wave<5 && ports_.population.questBirthComplete(p.area,"Baal Subject "+std::to_string(world.wave+1))) {
                ++world.wave;world.wavePrepared=false;world.waveAt=tick.tick+100;
            }
            // Missing population outside the throne must not block its waves.
            // A missing nearby placement remains a real uncleared objective.
            bool occupied=area.definition.populationMissing!=area.definition.populationMissingPositions.size();
            for(const auto at:area.definition.populationMissingPositions) if((at-unit->position).length()<64) occupied=true;
            for(const auto &[monsterId,m]:ports_.monsters.read().actors) {(void)monsterId;if(m.area==p.area && !m.questNpc && !m.owner && m.life>0 && (m.position-unit->position).length()<64) occupied=true;}
            const auto admitted=ports_.population.read().areas.find(p.area);
            if(admitted==ports_.population.read().areas.end()) occupied=true;
            else for(const auto &pending:area.definition.population) if(!admitted->second.admittedSpawnKeys.contains(pending.identity.spawnKey) &&
                !admitted->second.rejectedDensityKeys.contains(pending.identity.spawnKey) && (pending.position-unit->position).length()<64) occupied=true;
            if(admitted!=ports_.population.read().areas.end()) for(size_t index=0;index<area.definition.activation.rooms().size();++index) {
                const auto &room=area.definition.activation.rooms()[index];if(!room.populate) continue;
                const float nearestX=std::clamp(unit->position.x,float(room.x),float(room.x+room.width));
                const float nearestY=std::clamp(unit->position.y,float(room.y),float(room.y+room.height));
                if((Vec{nearestX,nearestY}-unit->position).length()<64 &&
                    (!admitted->second.roomStages.contains(index) || admitted->second.roomStages.at(index)<3)) occupied=true;
            }
            if(occupied || (world.waveAt && tick.tick<world.waveAt)) continue;
            if(!world.wavePrepared) {world.wavePrepared=true;world.waveAt=tick.tick+250;continue;}
            if(world.wave<5) {
                const auto spawned=spawnQuestGroup(actor,"Baal Subject "+std::to_string(world.wave+1));
                if(spawned) {++world.wave;world.wavePrepared=false;world.waveAt=tick.tick+100;}else blocked=true;
            } else world.throneDeparted=true;
        }
        if(int(p.area)==132 && !world.exitPortal && p.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::EveOfDestruction)].flags&baalTyraelSpoken) {
            const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::BaalExit,0);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);world.exitPortal=true;}else blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
