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
#include <algorithm>
#include "gameplay/quest/acts/act_five_state.hpp"
#include "core/random.hpp"
namespace d2x::server::quests {
DomainResult<> System::baalThroneClassHints(EntityId id,unsigned wave,uint64_t tick) {
    const auto *source=ports_.monsters.find(id);
    if(!source || source->rule.ai.kind!=MonsterAiKind::BaalThrone || wave>=5) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(source->area).definition;
    const auto preset=area.nativeQuestMonsters.find("Baal Subject "+std::to_string(wave+1));
    if(preset==area.nativeQuestMonsters.end()) return {DomainStatus::NotImplemented,{}};
    std::vector<DomainFact> facts{MonsterClassHintFact{id,source->area,uint16_t(preset->second.definition)}};
    // SUnitMsg::sub_6FCC6470 queues A4; it does not remove any server corpse.
    const auto extra=wave==0?"fallen5":wave==1?"skmage_cold3":"";
    if(*extra) {
        const auto found=std::find_if(area.nativeMonsterClasses.begin(),area.nativeMonsterClasses.end(),[&](const auto &entry){return entry.second.identity.monster==extra;});
        if(found==area.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
        facts.push_back(MonsterClassHintFact{id,source->area,uint16_t(found->first)});
    }
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},source->area},std::move(facts)});
    return sent?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{sent.status,{}};
}
DomainResult<> System::baalThroneSound(EntityId id,uint64_t tick) {
    const auto *source=ports_.monsters.find(id);
    if(!source || source->rule.ai.kind!=MonsterAiKind::BaalThrone) return {DomainStatus::InvalidActor,{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},source->area},{SoundFact{id,1,source->area,16}}});
    return sent?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{sent.status,{}};
}
DomainResult<> System::openWorldstoneChamber(EntityId source) {
    const auto *unit=ports_.monsters.find(source);
    if(!unit || unit->rule.ai.kind!=MonsterAiKind::BaalToStairs) return {DomainStatus::InvalidActor,{}};
    const auto removed=ports_.monsters.remove(source);if(!removed) return removed;
    state_.actFive.throneDeparted=true;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::spawnBaalWave(RegionId region,Vec position,int mappedPreset,uint64_t tick) {
    const auto *area=ports_.areas.find(region);if(!area) return {DomainStatus::Unavailable,{}};
    for(const auto &[name,preset]:area->definition.nativeQuestMonsters) if(preset.mappedClass==mappedPreset) {
        const auto born=ports_.population.requestQuestBirth(region,name,{tick},position);
        if(born.status==DomainStatus::Conflict || born.status==DomainStatus::Stale) return {DomainStatus::Capacity,{}};
        if(born.status==DomainStatus::Unavailable) return {DomainStatus::Applied,std::monostate{}};
        return born;
    }
    return {DomainStatus::NotImplemented,{}};
}
DomainResult<> System::initializeZoo(RegionId region,uint64_t tick) {
    if(state_.actFive.zooMonster) return {DomainStatus::Applied,std::monostate{}};
    const auto &candidates=ports_.areas.at(region).definition.zooCandidates;
    if(candidates.size()<2 || candidates.size()>UINT16_MAX || !state_.controlRandom)
        return {DomainStatus::NotImplemented,{}};
    auto random=*state_.controlRandom;
    uint16_t selected=0;
    for(int attempt=0;attempt<10;++attempt) {
        const auto candidate=1+limitedRandom(random,uint32_t(candidates.size()-1));
        if(candidates[candidate]) {selected=uint16_t(candidate);break;}
    }
    std::vector<EventBatch> notifications;
    for(const auto &[id,player]:ports_.players.all()) {
        if(!player.entered) continue;
        auto notice=fact(id,player.persistent.player,player.area);
        notice.zooMonster=selected;notice.zooOnly=true;
        notifications.push_back({0,tick,{}, {AudienceKind::Player,id,player.area},{std::move(notice)}});
    }
    if(!notifications.empty()) {
        const auto sent=ports_.events.publishGroup(std::move(notifications));
        if(!sent) return {sent.status,{}};
    }
    state_.controlRandom=random;state_.actFive.zooMonster=selected;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::baalFx(RegionId region,Vec position) {
    auto &state=state_.actFive;
    const bool active=(state_.initialFlags[questDefinition(QuestId::EveOfDestruction).nativeSlot]&0x8000)==0;
    if(active && !state.tyraelSpawned) {
        // 1.13c ACT5Q6_SpawnTyrael RVA 5c1f0 passes radius 100, not 1.10f's 19.
        if(const auto free=ports_.world.questFreePosition(region,position,position-Vec{5,5},5,100)) {
            const auto result=ports_.world.admitQuestNpc(free->area,"tyrael3",free->position,4);
            if(isDeferred(result.status)) return {result.status,{}};
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
            if(isDeferred(result.status)) blocked=true;
            else {
                world.baalFxStarted=true;
                if(result) ports_.world.retainControllerRoom(source->area,world.baalPosition,true);
            }
        } else world.baalFxStarted=true;
    }
    for(const auto &[id,p]:ports_.players.all()) {
        if(!p.entered || p.persistent.player.hp<=0) continue;
        const auto &area=ports_.areas.at(p.area);const ActorContext actor{id,p.actor,p.area,area.generation,0,tick.tick};
        if(int(p.area)==132 && !world.exitPortal && p.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::EveOfDestruction)].flags&baalTyraelSpoken) {
            const auto portal=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::BaalExit,0);
            if(portal) {ports_.travel.commitSpecialPortal(*portal.value);world.exitPortal=true;}else blocked=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
