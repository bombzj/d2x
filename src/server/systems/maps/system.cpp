#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "timed_treasure.hpp"

namespace d2x::server::maps {
int System::selectPreset(int marker) {
    synchronize();
    if(state_.instance && state_.instance->controller==45 && marker==738) return -2;
    if(state_.instance && state_.instance->controller==41 && (marker==738 || marker==739)) {
        constexpr std::array classes{704,705,709};
        for(size_t index=0;index<classes.size();++index) {
            const auto bit=uint32_t(2u<<index);
            if(!(state_.eventProgress&bit) && (marker==739 || pd2CrtRandom(ports_.random)%20==0)) {
                state_.eventProgress|=bit;return classes[index];
            }
        }
        return -2; // The native wrapper suppresses this authored marker.
    }
    const auto pool=pd2::outerVoidBossPool(marker);
    if(!state_.instance || state_.instance->controller!=33 || pool.empty()) return -1;
    return pool[pd2CrtRandom(ports_.random)%pool.size()];
}
void System::synchronize() {
    const auto &travel=ports_.travel.read();
    if(!travel.pd2MapPortal || !travel.pd2Map || state_.instance==travel.pd2Map) return;
    for(const auto &[key,job]:state_.timedTreasures) {
        if(job.retained) ports_.world.retainControllerUnitRoom(key.first,job.area,job.position,false,pd2TreasureTimerHandler);
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,key.first},FrameEventType::MonsterModifier,pd2TreasureTimerHandler);
    }
    for(const auto &[key,job]:state_.wandererBirths) {
        if(!job.complete) ports_.world.retainControllerUnitRoom(key.first,job.area,job.source,false,pd2WandererTimerHandler);
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,key.first},FrameEventType::MonsterModifier,pd2WandererTimerHandler);
    }
    for(const auto &[key,job]:state_.objectTreasures) {
        if(job.retained) ports_.world.retainControllerUnitRoom(key.first,job.area,job.position,false,pd2ObjectTreasureTimerHandler);
        ports_.frameEvents.cancelHandler({FrameEventUnit::Object,key.first},FrameEventType::MonsterModifier,pd2ObjectTreasureTimerHandler);
    }
    for(const auto &binding:state_.bindings) if(binding.id)
        ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);
    state_={};state_.instance=travel.pd2Map;
}
void System::bind(int slot,EntityId id,RegionId area,Vec position,bool object) {
    synchronize();
    if(slot<0 || slot>=int(state_.bindings.size()) || !state_.instance) return;
    auto &binding=state_.bindings[size_t(slot)];
    if(binding.id==id) return;
    if(binding.id) ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);
    binding={id,area,position,object};
    if(!id) return;
    if(!object || state_.phase!=2) ports_.world.retainControllerUnitRoom(id,area,position,true);
    if(slot==0 && !object && state_.instance->controller==9 && !state_.phase)
        if(const auto *actor=ports_.monsters.find(id);actor &&
           (actor->identity.nativeClass==743 || actor->identity.nativeClass==1104 || actor->identity.nativeClass==1105)) state_.phase=1;
}
bool System::handles(const monsters::Actor &unit) const {
    if(!state_.instance) return false;
    if(state_.instance->controller==41) return unit.identity.nativeClass==704 || unit.identity.nativeClass==705 || unit.identity.nativeClass==709;
    if(state_.instance->controller==42) return unit.identity.nativeClass==789;
    if(state_.instance->controller==43) return unit.identity.nativeClass>=933 && unit.identity.nativeClass<=936;
    if(state_.instance->controller==45) return unit.identity.nativeClass==1112;
    if(state_.instance->controller==44) return unit.identity.nativeClass>=989 && unit.identity.nativeClass<=991;
    if(pd2::mapBossGroup(unit.identity.nativeClass)) return true;
    if(state_.instance->controller==9) return state_.bindings[0].id==unit.id &&
        (unit.identity.nativeClass==743 || unit.identity.nativeClass==1104 || unit.identity.nativeClass==1105);
    if(state_.instance->controller==31) return unit.identity.nativeClass==1060;
    if(state_.instance->controller==30) return unit.identity.nativeClass==1058 ||
        (unit.identity.nativeClass>=1061 && unit.identity.nativeClass<=1063);
    if(state_.instance->controller==35) return unit.identity.nativeClass==1174;
    if(state_.instance->controller==34) return unit.identity.nativeClass==1148 || unit.identity.nativeClass==1166;
    return state_.instance->controller==32 && (unit.identity.nativeClass==1094 || unit.identity.nativeClass==1096);
}
bool System::controllerDeathReady(const monsters::Actor &unit) const {
    if(!handles(unit)) return true;
    const auto found=state_.deaths.find(unit.id);
    return found!=state_.deaths.end() && found->second.occurrence==unit.deathOccurrence && found->second.phase==Death::Phase::Complete &&
        (!(pd2::mapBossGroup(unit.identity.nativeClass)>=1 && pd2::mapBossGroup(unit.identity.nativeClass)<=4) || found->second.tierRewardPhase==5);
}
bool System::deathReady(const monsters::Actor &unit) const {
    if(!controllerDeathReady(unit)) return false;
    if(!handlesEventDeath(unit)) return true;
    const auto found=state_.eventDeaths.find(unit.id);
    return found!=state_.eventDeaths.end() && found->second.occurrence==unit.deathOccurrence && found->second.phase==Death::Phase::Complete;
}
bool System::skipsTreasure(const monsters::Actor &unit) const {
    if(const auto event=state_.eventDeaths.find(unit.id);event!=state_.eventDeaths.end() &&
        event->second.occurrence==unit.deathOccurrence && event->second.suppressTreasure) return true;
    const auto found=state_.deaths.find(unit.id);
    return found!=state_.deaths.end() && found->second.occurrence==unit.deathOccurrence && found->second.suppressTreasure;
}
DomainResult<> System::advanceDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    const int group=pd2::mapBossGroup(unit.identity.nativeClass);
    if(state_.instance->controller>=41 && state_.instance->controller<=45) return advanceEndgameDeath(unit,death,tick);
    if(group>=1 && group<=4) {
        if(state_.instance->controller==33 && death.phase!=Phase::Complete) {
            const auto stage=advanceOuterVoidDeath(unit,death,tick);
            if(!stage || death.phase!=Phase::Complete) return stage;
        } else death.phase=Phase::Complete;
        return advanceTierBossRewards(unit,death,tick);
    }
    if(group==5 && (state_.instance->controller!=9 || state_.bindings[0].id!=unit.id)) {
        death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
    }
    if(state_.instance->controller==34 && unit.identity.nativeClass==1166 && state_.bindings[1].id==unit.id)
        bind(1,{},{},{},false);
    if(state_.instance->controller==30 && !death.evaluated) {
        death.evaluated=true;
        if(state_.phase>=4) death.phase=Phase::Complete;
        else if(unit.identity.nativeClass!=1058)
            death.phase=(pd2CrtRandom(ports_.random)&1)?Phase::Complete:Phase::Reward;
    }
    if(death.phase==Phase::Complete) return {DomainStatus::Applied,std::monostate{}};
    if(state_.instance->controller==9) {
        const size_t count=unit.identity.nativeClass==743?7:unit.identity.nativeClass==1104?2:1;
        for(size_t slot=1;slot<=count;++slot) {
            auto &binding=state_.bindings[slot];if(!binding.id || !binding.object) continue;
            const auto opened=ports_.objects.openNativeBlocker(binding.id,tick.tick);
            if(isDeferred(opened.status)) return opened;
            ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);binding={};
        }
        auto &owner=state_.bindings[0];
        ports_.world.retainControllerUnitRoom(owner.id,owner.area,owner.position,false);owner={};
        if(state_.phase==1) state_.phase=2;
        death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
    }
    if(death.phase==Phase::Counter) {
        if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const ActorContext actor{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
            const auto result=ports_.transactions.incrementPd2Counter(actor,14);
            if(isDeferred(result.status)) return result;
        }
        death.phase=Phase::Portal;
        if(state_.instance->controller==30) state_.phase=4;
    }
    if(death.phase==Phase::Portal) {
        if(state_.instance->controller==34 && unit.identity.nativeClass==1148) {
            const pd2::MapPortalProgram program{194,0,587,1,1,{-5,-5},false};
            const auto portal=advanceDeathPortal(unit.area,death,program,584,unit.position,0,tick);
            if(!portal) return portal;
            death.phase=Phase::Reward;
        }
    }
    if(death.phase==Phase::Portal) {
        const bool final=state_.instance->controller!=32 || unit.identity.nativeClass==1094;
        const int destination=final?109:184,object=final?586:584;
        const auto prepared=ports_.world.requestMapArea(RegionId(destination),state_.instance);
        if(!prepared) return {prepared.status,{}};
        if(!ports_.areas.find(RegionId(destination))) return {DomainStatus::PendingContent,{}};
        if(!death.portalPosition) {
            const auto point=ports_.world.nativeFreeSpace(unit.area,unit.position,unit.position,1);
            if(point && point->area==unit.area) death.portalPosition=point->position;
        }
        if(death.portalPosition) {
            auto portal=ports_.travel.prepareNativePermanentPortal(unit.area,*death.portalPosition,destination,tick.tick,object);
            if(isDeferred(portal.status)) return {portal.status,{}};
            if(portal) {
                const auto &link=portal.value->next.rbegin()->second;
                ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);
                ports_.world.retainControllerRoom(link.town,link.townPosition,true);
                ports_.travel.commitSpecialPortal(std::move(*portal.value));
            }
        }
        death.phase=final?Phase::Reward:Phase::Complete;
    }
    if(state_.instance->controller==31 || state_.instance->controller==35)
        return advanceRandomRewards(unit,death,tick);
    if(state_.instance->controller==30) return advanceCharmReward(unit,death,tick);
    if(state_.instance->controller==34) return advanceImperialReward(unit,death,tick);
    if(death.phase==Phase::Reward) {
        if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
        if(!death.rewardContext) {death.suppressTreasure=true;death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};}
        if(!death.reward) death.reward=size_t(392+pd2CrtRandom(ports_.random)%8);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
        loot::NativeDrop drop;drop.uniqueRow=death.reward;drop.resetUnique=true;drop.crtInitialization=true;
        drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
        if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
        request.nativeDrops.push_back(std::move(drop));
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;
        death.phase=Phase::Wait;
    }
    if(death.phase==Phase::Wait) {
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    const auto &travel=ports_.travel.read();
    if(travel.pd2MapPortal && travel.pd2Map) synchronize();
    if(!travel.pd2MapPortal || !travel.pd2Map) {synchronizeTimedTreasures(tick);return StepStatus::Complete;}
    for(auto &binding:state_.bindings) if(binding.id && !binding.object) {
        const auto *actor=ports_.monsters.find(binding.id);
        if(!actor || (!actor->alive() && controllerDeathReady(*actor))) {
            ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);
        } else if(actor->area!=binding.area || actor->position.x!=binding.position.x || actor->position.y!=binding.position.y) {
            ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,false);
            binding.area=actor->area;binding.position=actor->position;
            ports_.world.retainControllerUnitRoom(binding.id,binding.area,binding.position,true);
        }
    }
    bool pending=advancePortals(tick);
    pending|=!removeSceneWaypoints();
    pending|=!initializeSceneTown(tick);
    pending|=advanceEventOperations(tick);
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        if(unit.alive() || unit.rewardComplete || !handles(unit)) continue;
        auto [found,created]=state_.deaths.try_emplace(id);
        if(created || found->second.occurrence!=unit.deathOccurrence) {found->second={};found->second.occurrence=unit.deathOccurrence;}
        if(!controllerDeathReady(unit)) {
            const auto result=advanceDeath(unit,found->second,tick);pending|=!result;
        }
    }
    pending|=advanceEventDeaths(tick);
    // Content/loot preparation advances between frames. Death asks deathReady
    // per victim; unrelated deaths and the Loot phase must keep progressing.
    synchronizeTimedTreasures(tick);
    return pending?StepStatus::Blocked:StepStatus::Complete;
}
}
