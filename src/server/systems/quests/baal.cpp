#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
namespace d2x::server::quests {
StepStatus System::baalStep(TickContext tick) {
    bool blocked=false;auto &world=state_.actFive;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        (void)id;
        if(int(m.area)==132 && m.identity.monster=="baalcrab" && !m.owner && m.identity.origin!=SpawnOrigin::Debug && m.identity.origin!=SpawnOrigin::Summoned && m.life<=0) {
            world.baalSlain=true;world.baalPosition=m.position;captureGoal(QuestId::EveOfDestruction,5,m.area);
        }
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || p.persistent.player.hp<=0) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        if(int(p.area)==131 && !world.throneDeparted) for(const auto &throne:area.definition.npcs) if(throne.rule.code=="baalthrone" && (p.position-throne.position).length()<64) {
            // Missing population outside the throne must not block its waves.
            // A missing nearby placement remains a real uncleared objective.
            bool occupied=area.definition.populationMissing!=area.definition.populationMissingPositions.size();
            for(const auto at:area.definition.populationMissingPositions) if((at-throne.position).length()<64) occupied=true;
            for(const auto &[monsterId,m]:ports_.monsters.read().actors) {(void)monsterId;if(m.area==p.area && !m.owner && m.life>0 && (m.position-throne.position).length()<64) occupied=true;}
            const auto admitted=ports_.population.read().areas.find(p.area);
            if(admitted==ports_.population.read().areas.end()) occupied=true;
            else for(const auto &pending:area.definition.population) if(!admitted->second.admittedSpawnKeys.contains(pending.identity.spawnKey) && (pending.position-throne.position).length()<64) occupied=true;
            if(occupied || (world.waveAt && tick.tick<world.waveAt)) continue;
            if(!world.wavePrepared) {world.wavePrepared=true;world.waveAt=tick.tick+250;continue;}
            if(world.wave<5) {
                const auto spawned=spawnQuestGroup(actor,"Baal Subject "+std::to_string(world.wave+1));
                if(spawned) {++world.wave;world.wavePrepared=false;world.waveAt=tick.tick+100;}else blocked=true;
            } else world.throneDeparted=true;
        }
        if(int(p.area)==132 && world.baalSlain && !world.tyraelSpawned) {
            if(ports_.world.admitQuestNpc(p.area,"tyrael3",world.baalPosition+Vec{5,0})) world.tyraelSpawned=true;else blocked=true;
        }
        if(int(p.area)==132 && !world.exitPortal && p.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::EveOfDestruction)].flags&baalTyraelSpoken) {
            const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::BaalExit,0);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);world.exitPortal=true;}else blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
