#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <vector>

namespace d2x::server::quests {
DomainResult<> System::readAncientAltar(const ActorContext &actor,EntityId source) {
    auto &world=state_.actFive;
    if(world.ancientsActive || world.ancientsDefeated) return {DomainStatus::Applied,std::monostate{}};
    const auto altar=ports_.objects.read().objects.find(source);
    if(altar==ports_.objects.read().objects.end() || altar->second.area!=actor.area || altar->second.rule.operation!=65 || source!=world.ancientAltar)
        return {DomainStatus::Stale,{}};
    const auto &groups=ports_.areas.at(actor.area).definition.nativeQuestMonsters;
    for(const auto name:ancientIdentities) if(!groups.contains(name)) return {DomainStatus::Unavailable,{}};
    std::array<EntityId,3> statues;
    for(const auto &[id,object]:ports_.objects.read().objects) {
        if(object.area!=actor.area || object.mode || !ports_.areas.loaded(object.area,object.position)) continue;
        const int slot=object.rule.operation==63?0:object.rule.operation==64?1:object.rule.operation==62?2:-1;
        if(slot>=0) statues[size_t(slot)]=id;
    }
    if(std::any_of(statues.begin(),statues.end(),[](EntityId id){return !id;})) return {DomainStatus::Unavailable,{}};
    world.ancientsActive=true;world.ancientAt=actor.tick+20;world.ancientRestoreAt=0;
    for(size_t index=0;index<statues.size();++index) {
        world.ancients[index]={statues[index],{},world.ancientAt};state_.objectModes[statues[index]]=3;
    }
    // A5Q5's registered GUID slots are statue2, statue3, statue1.
    ports_.objects.activateAncientStatues({statues[1],statues[2],statues[0]},actor.tick);
    return {DomainStatus::Applied,std::monostate{}};
}
uint64_t System::ancientStatueDue(EntityId id) const {
    if(state_.actFive.ancientsActive) for(const auto &ancient:state_.actFive.ancients)
        if(ancient.statue==id) return ancient.birthAt;
    return 0;
}
uint64_t System::ancientObjectAnimationEnd(EntityId id) const {
    if(state_.actFive.ancientAltar==id) return state_.actFive.ancientAltarEnd;
    for(const auto &ancient:state_.actFive.ancients) if(ancient.statue==id) return ancient.animationEnd;
    return 0;
}
void System::finishAncientObjectAnimation(EntityId id) {
    auto &world=state_.actFive;
    if(world.ancientAltar==id && world.ancientAltarEnd) {
        world.ancientAltarEnd=0;state_.objectModes[id]=2;
    }
    for(auto &ancient:world.ancients) if(ancient.statue==id && ancient.animationEnd) {
        ancient.animationEnd=0;state_.objectModes[id]=2;
    }
}
DomainResult<> System::ancientStatueEvent(EntityId id,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    auto &world=state_.actFive;
    if(!world.ancientsActive || world.ancientsDefeated) return done();
    for(size_t index=0;index<world.ancients.size();++index) {
        auto &ancient=world.ancients[index];if(ancient.statue!=id || !ancient.birthAt || tick<ancient.birthAt) continue;
        const auto object=ports_.objects.read().objects.find(id);if(object==ports_.objects.read().objects.end()) return done();
        const auto &statue=object->second;
        if(statue.mode==4 || ancient.spawned) {ancient.birthAt=0;return done();}
        // A5Q5 uses a separate QUESTFN event for each statue, at its actual
        // position. A failed native placement retries this statue after10.
        const auto result=ports_.population.requestQuestBirth(statue.area,ancientIdentities[index],{tick},statue.position);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale)
            return {DomainStatus::Capacity,{}};
        if(result.status==DomainStatus::NotImplemented) return result;
        if(!result) {ancient.birthAt=tick+10;return done();}
        const auto key="quest."+std::string(ancientIdentities[index]);
        for(const auto &[monster,actor]:ports_.monsters.read().actors)
            if(actor.area==statue.area && actor.identity.spawnKey==key) {ancient.monster=monster;break;}
        if(!ancient.monster) return {DomainStatus::Stale,{}};
        ancient.spawned=true;ancient.birthAt=0;state_.objectModes[id]=4;
        return done();
    }
    return done();
}
void System::resetAncients() {
    auto &world=state_.actFive;
    if(world.ancientsDefeated || (!world.ancientsActive && !world.ancientAltar)) return;
    for(const auto name:ancientIdentities) {
        ports_.monsters.withdrawQuestGroup(RegionId(120),name);
        ports_.population.resetQuestBirth(RegionId(120),name);
    }
    for(const auto &ancient:world.ancients) if(ancient.statue) state_.objectModes[ancient.statue]=0;
    if(world.ancientAltar) state_.objectModes[world.ancientAltar]=0;
    world.ancientsActive=false;world.ancientAt=world.ancientRestoreAt=world.ancientAltarEnd=0;
    world.ancients={};
    world.ancientAltarReaders.clear();world.ancientAltar={};
}
StepStatus System::ancientsStep(TickContext tick) {
    auto &world=state_.actFive;if(!world.ancientsActive && !world.ancientsDefeated && !world.ancientAltar) return StepStatus::Complete;
    std::erase_if(world.ancientAltarReaders,[&](const auto &entry) {
        const auto *player=ports_.players.find(entry.first);
        return !player || !player->entered || player->actor!=entry.second.actor || int(player->area)!=120 ||
            player->persistent.player.hp<=0 || ports_.areas.at(player->area).generation!=entry.second.generation;
    });
    bool alivePlayer=false;
    for(const auto &[id,player]:ports_.players.all()) {(void)id;if(player.entered && int(player.area)==120 && player.persistent.player.hp>0) alivePlayer=true;}
    if(!alivePlayer && !world.ancientsDefeated) {
        const bool finished=std::all_of(world.ancients.begin(),world.ancients.end(),[&](const auto &ancient) {
            const auto *unit=ports_.monsters.find(ancient.monster);return ancient.spawned && unit && !unit->alive();
        });
        if(!finished) {resetAncients();return StepStatus::Complete;}
    }
    std::vector<std::pair<const monsters::Actor *,size_t>> deaths;
    for(size_t index=0;index<world.ancients.size();++index) {
        const auto &ancient=world.ancients[index];
        if(const auto *unit=ports_.monsters.find(ancient.monster);unit && !unit->alive() && !ancient.deathFx)
            deaths.emplace_back(unit,index);
    }
    std::sort(deaths.begin(),deaths.end(),[](const auto &left,const auto &right) {
        return std::pair{left.first->deathTick,left.first->deathOccurrence}<std::pair{right.first->deathTick,right.first->deathOccurrence};
    });
    for(const auto &[unit,index]:deaths) {
        auto &ancient=world.ancients[index];
        if(!ancient.dead) {
            ancient.dead=true;ancient.restorePending=true;
            // Monster death precedes QuestUpdater. On a multiple of20 the
            // current frame's update is the first boundary after CreateTimer.
            if(!world.ancientRestoreAt) world.ancientRestoreAt=((tick.tick?tick.tick-1:0)/20+3)*20;
        }
        const auto statue=ports_.objects.read().objects.find(ancient.statue);
        if(!ancient.deathFxMissile && statue!=ports_.objects.read().objects.end()) {
            const auto definition=ports_.missiles.nativeDefinition("ancient death center");if(!definition) return StepStatus::NotImplemented;
            missiles::NativeSpawn spawn{unit->id,unit->area,ports_.areas.at(unit->area).generation,tick.tick,
                unit->position,statue->second.position,*definition,0,1};
            spawn.distanceCountdown=true;spawn.finalParameters=std::array{int32_t(uint32_t(ancient.statue.value)),int32_t{0}};
            const auto result=ports_.missiles.spawnNative(spawn);
            if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return StepStatus::Blocked;
            if(result.status==DomainStatus::NotImplemented) return StepStatus::NotImplemented;
        }
        ancient.deathFxMissile=true;
        std::vector<EventBatch> effects;
        for(const auto &[player,body]:ports_.players.all()) if(body.entered)
            effects.push_back({0,tick.tick,{}, {AudienceKind::Player,player,body.area},
                {fact(player,body.persistent.player,body.area),QuestEffectFact{18}}});
        if(!effects.empty() && !ports_.events.publishGroup(std::move(effects))) return StepStatus::Blocked;
        ancient.deathFx=true;
    }
    if(world.ancientRestoreAt && tick.tick>=world.ancientRestoreAt) {
        bool missing=false;
        for(auto &ancient:world.ancients) if(ancient.restorePending) {
            const auto statue=ports_.objects.read().objects.find(ancient.statue);
            if(statue==ports_.objects.read().objects.end()) {missing=true;continue;}
            state_.objectModes[ancient.statue]=1;
            ancient.animationEnd=tick.tick+std::max(uint64_t{1},statue->second.rule.openingTicks-1);
            ancient.restorePending=false;
        }
        world.ancientRestoreAt=missing?(tick.tick/20+3)*20:0;
    }
    const bool cleared=std::all_of(world.ancients.begin(),world.ancients.end(),[](const auto &ancient){return ancient.spawned && ancient.dead && ancient.deathFx;});
    if(cleared && !world.ancientsDefeated) {
        const monsters::Actor *last=nullptr;
        for(const auto &ancient:world.ancients) if(const auto *unit=ports_.monsters.find(ancient.monster);
            unit && (!last || std::pair{unit->deathTick,unit->deathOccurrence}>std::pair{last->deathTick,last->deathOccurrence})) last=unit;
        captureGoal(QuestId::RiteOfPassage,4,RegionId(120),{},last?killerPlayer(last->killer):std::nullopt);
        world.ancientsDefeated=true;world.ancientsActive=false;
    }
    return StepStatus::Complete;
}
}
