#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::advanceBossSceneDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    const int controller=state_.instance->controller,difficulty=state_.instance->bossDifficulty,type=unit.identity.nativeClass;
    if(controller!=42 && controller!=43 && controller!=45) return {DomainStatus::NotImplemented,{}};
    if(death.phase==Phase::Counter) {
        if(controller==43 && (type==933 || type==934)) {
            const int limit=type==934?2:4;
            if(state_.phase<limit || state_.sceneRetreats.contains(unit.id)) {
                const auto portal=rathmaRetreatPortal(unit.id,tick);if(!portal) return portal;
            }
            death.phase=Phase::Complete;
        } else if(controller==43 && state_.phase==5) {
            state_.phase=6;death.eventDestination=6;death.phase=Phase::Reward;
        } else if(controller==43 && state_.phase!=6) death.phase=Phase::Complete;
        else {
            if(controller==43) state_.phase=7;
            death.phase=controller==43?Phase::Cleanup:Phase::Portal;
        }
    }
    if(controller==43 && death.eventDestination==6) {
        while(death.cleanupIndex<2) {
            const auto id=state_.bindings[death.cleanupIndex].id;
            const auto *partner=ports_.monsters.find(id);
            if(partner && partner->nativeMode!=0 && partner->nativeMode!=12) {
                const auto state=ports_.effects.pd2SceneSkillState(unit.id,id,485,1,tick.tick);if(!state) return state;
            }
            ++death.cleanupIndex;
        }
        death.phase=Phase::Complete;
    }
    if(death.phase==Phase::Complete) return {DomainStatus::Applied,std::monostate{}};
    const auto cleanup=[&]()->DomainResult<> {
        if(!death.cleanupPrepared) {
            death.cleanupPrepared=true;
            if(controller==45) state_.phase=8;
            for(const auto &[id,other]:ports_.monsters.read().actors) if(other.alive()) {
                const int child=other.identity.nativeClass;
                const bool selected=controller==45?int(other.area)==188 && (child==1114 || child==1115 || child==1140 || child==1141):
                    child==968 || child==969 || child==1106 || child==1107 || child==937 || child==938 || child==939;
                if(selected) death.cleanup.push_back(id);
            }
        }
        if(controller==45 && state_.bindings[1].id) {
            const auto removed=ports_.monsters.remove(state_.bindings[1].id);if(isDeferred(removed.status)) return removed;
            bind(1,{},{},{},false);
        }
        while(death.cleanupIndex<death.cleanup.size()) {
            const auto killed=ports_.monsters.scriptDeath(death.cleanup[death.cleanupIndex],tick.tick);
            if(isDeferred(killed.status)) return killed;
            ++death.cleanupIndex;
        }
        return {DomainStatus::Applied,std::monostate{}};
    };
    if(death.phase==Phase::Cleanup) {
        const auto cleaned=cleanup();if(!cleaned) return cleaned;
        death.phase=Phase::Portal;
    }
    if(death.phase==Phase::Portal) {
        if(controller==45 && death.eventStep==0) {
            if(!death.evaluated) {
                if(!death.portalPosition) {
                    death.portalPosition=ports_.world.nativeActSpawnLocation(RegionId(109),1);
                    if(!death.portalPosition) death.evaluated=true;
                }
                if(death.portalPosition) {
                    ports_.world.activate(RegionId(109),*death.portalPosition);
                    if(!ports_.areas.loaded(RegionId(109),*death.portalPosition)) return {DomainStatus::PendingContent,{}};
                    const auto point=ports_.world.nativeActSpawnLocationExAt(RegionId(109),*death.portalPosition,3);
                    death.portalPosition=point?std::optional{point->position}:std::nullopt;
                    death.evaluated=true;
                }
            }
            if(death.portalPosition) {
                const pd2::MapPortalProgram program{188,0,587,1,1,{-5,-5},false};
                const auto linked=advanceDeathPortal(RegionId(109),death,program,622,*death.portalPosition,0,tick);
                if(!linked) return linked;
            }
            death.eventStep=1;death.evaluated=false;death.portalPosition.reset();
        }
        if(!death.evaluated) {
            const auto point=controller==45?Vec{0x6221,0x17df}-ports_.areas.at(unit.area).definition.origin:unit.position;
            ports_.world.activate(unit.area,point);
            if(!ports_.areas.loaded(unit.area,point)) return {DomainStatus::PendingContent,{}};
            if(const auto free=ports_.world.nativeFreeSpace(unit.area,point,point,1)) death.portalPosition=free->position;
            death.evaluated=true;
        }
        if(death.portalPosition) {
            const int destination=controller==45?189:109,definition=controller==45?622:586;
            const auto requested=ports_.world.requestMapArea(RegionId(destination),state_.instance);
            if(!requested) return {requested.status,{}};
            if(!ports_.areas.find(RegionId(destination))) return {DomainStatus::PendingContent,{}};
            auto portal=ports_.travel.prepareNativePermanentPortal(unit.area,*death.portalPosition,destination,tick.tick,definition);
            death.portalStatus=int(portal.status);
            if(isDeferred(portal.status)) return {portal.status,{}};
            if(portal) {
                const auto &link=portal.value->next.rbegin()->second;
                ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);
                ports_.world.retainControllerRoom(link.town,link.townPosition,true);
                ports_.travel.commitSpecialPortal(std::move(*portal.value));
            }
        }
        death.eventStep=0;death.phase=Phase::Reward;
    }
    const int slots=controller==42?2:controller==43?3:1;
    while(death.eventStep<slots && (death.phase==Phase::Reward || death.phase==Phase::Wait)) {
        if(death.phase==Phase::Reward) {
            if(death.eventChoice<0) {
                auto random=unit.combatRandom;bool selected=true;int choice=0;
                if(controller==42 && death.eventStep==1) {
                    selected=difficulty!=0 && pd2UnitEventRandom(random)%200==0;
                    if(selected) choice=int(pd2UnitEventRandom(random)%5);
                } else if(controller==43 && death.eventStep>0) {
                    selected=death.eventStep==1?pd2UnitEventRandom(random)%uint32_t(50-difficulty*6)==0:
                        difficulty!=0 && pd2UnitEventRandom(random)%200==0;
                    if(selected) choice=int(pd2UnitEventRandom(random)%3);
                } else if(controller==45)
                    selected=difficulty!=0 && pd2UnitEventRandom(random)%uint32_t((4-difficulty)*25)==0;
                ports_.monsters.commitCombatRandom(unit.id,random);
                if(!selected) {++death.eventStep;continue;}
                death.eventChoice=choice;
            }
            if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
            LootRequest source;source.source=unit.id;source.identity=unit.identity;source.region=unit.area;source.difficulty=unit.rule.difficulty;
            loot::Request request{unit.deathOccurrence,source,{},unit.position};request.environment=true;request.reportCompletion=true;
            loot::NativeDrop drop;drop.level=death.eventStep?131:130;
            if(controller==42) {
                if(!death.eventStep) drop.code="cm1";
                else if(death.eventChoice==1) {drop.uniqueRow=402;drop.crtInitialization=true;}
                else drop.code=std::array{"utb","","uth","7qr","7bs"}[size_t(death.eventChoice)];
            } else if(controller==43) drop.code=death.eventStep==0?"cwss":death.eventStep==1?
                std::array{"rid","rtp","rkey"}[size_t(death.eventChoice)]:std::array{"ram","rar","rbe"}[size_t(death.eventChoice)];
            else drop.code="imrn";
            drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
            if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
            request.nativeDrops.push_back(std::move(drop));
            const auto queued=ports_.loot.queue(request);if(!queued) return queued;death.phase=Phase::Wait;
        }
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        ++death.eventStep;death.eventChoice=-1;death.rewardPositionPrepared=false;death.rewardPosition.reset();death.phase=Phase::Reward;
    }
    if(controller==45) {const auto cleaned=cleanup();if(!cleaned) return cleaned;}
    if(death.tierRewardPhase==0) {
        if(ports_.travel.read().pd2SceneEntrants.size()==1) if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const auto actor=ActorContext{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
            const size_t counter=controller==42?0:controller==43?2:4;
            const uint32_t flag=difficulty==2?(controller==42?1u:controller==43?2u:8u):0;
            const auto counted=ports_.transactions.incrementPd2Counter(actor,counter,difficulty==2?std::optional{counter+1}:std::nullopt,flag);
            if(isDeferred(counted.status)) return counted;
        }
        death.tierRewardPhase=1;
    }
    if(difficulty==2 && ports_.travel.read().pd2SceneEntrants.size()==1) {
        if(const auto *killer=ports_.players.findActor(unit.killer);killer && killer->entered) {
            const auto aura=ports_.effects.pd2AchievementState(killer->actor,tick.tick);if(isDeferred(aura.status)) return aura;
        }
    }
    death.phase=Phase::Complete;return {DomainStatus::Applied,std::monostate{}};
}
}
