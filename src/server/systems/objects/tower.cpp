#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
namespace d2x::server::objects {
DomainResult<> System::openNativeBlocker(EntityId id,uint64_t tick) {
    const auto found=state_.objects.find(id);
    if(found==state_.objects.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &object=found->second;
    if(object.collisionReleased) return {DomainStatus::Applied,std::monostate{}};
    if(object.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    object.mode=1;object.until=tick+object.rule.openingTicks;object.collisionReleased=true;
    ++object.revision;collision(object.area);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::openTowerChest(EntityId id,uint64_t tick) {
    const auto found=state_.objects.find(id);
    if(found==state_.objects.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &object=found->second;
    if(towerFailed_.contains(id)) return {DomainStatus::NotImplemented,{}};
    if(towerOpening_.contains(id)) {
        const auto completion=ports_.loot.takeCompletion(id);
        if(!completion) return {DomainStatus::Conflict,{}};
        towerOpening_.erase(id);object.pending=false;
        if(*completion) {
            object.mode=object.rule.openingMode?1:2;
            object.until=object.rule.openingMode?tick+object.rule.openingTicks:0;
            object.rule.selectable.fill(false);
            ++object.revision;collision(object.area);
        } else towerFailed_.insert(id);
        return *completion?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::NotImplemented,{}};
    }
    if(object.mode) return {DomainStatus::Applied,std::monostate{}};
    if(object.pending || object.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    LootRequest source;source.source=id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
    ChestState chest=object.rule.chest.value_or(ChestState{});chest.lootSeed=object.random;
    loot::Request request{tick,source,{},object.position,loot::ObjectSource{object.definition,1,chest,{}}};
    request.environment=true;
    const auto result=ports_.loot.queue(request);if(!result) return result;
    towerOpening_.insert(id);object.pending=true;
    return {DomainStatus::Conflict,{}};
}
DomainResult<> System::towerGold(GroundLocation position,uint64_t tick) {
    const auto *area=ports_.areas.find(position.region);
    if(!area) return {DomainStatus::Unavailable,{}};
    const auto result=ports_.items.createNativeGold(position,unsigned(area->definition.monsterLevel),tick);
    return result?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{result.status,{}};
}
DomainResult<> System::openOrbMist(EntityId id,uint64_t tick) {
    const auto found=state_.objects.find(id);if(found==state_.objects.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &object=found->second;
    if(!object.mode && object.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    ports_.world.retainControllerRoom(object.area,object.position,false);
    if(!object.mode) {
        object.mode=1;object.until=tick+(object.rule.openingTicks?object.rule.openingTicks-1:0);
        ++object.revision;collision(object.area);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
void System::countessKilled(Vec position,uint64_t tick) {
    if(towerDeathPosition_) return;
    towerDeathPosition_=position;towerRetryAt_=tick;
}
StepStatus System::towerStep(TickContext tick) {
    if(!towerDeathPosition_ || towerFinished_ || tick.tick<towerRetryAt_) return StepStatus::Complete;
    if(!towerStarted_) {
        while(towerProbe_<towerChests_.size()) {
            const auto id=towerChests_[towerProbe_];
            const auto found=state_.objects.find(id);
            if(found==state_.objects.end() || !ports_.areas.loaded(found->second.area,found->second.position)) {
                ++towerProbe_;towerFallback_=false;continue;
            }
            const auto &object=found->second;const auto &area=ports_.areas.at(object.area);
            const auto prototype=area.definition.environmentMonsters.find("trap-firebolt");
            if(prototype==area.definition.environmentMonsters.end()) throw std::logic_error("Missing native tower controller");
            const auto attempt=[&](Vec position)->DomainResult<EntityId> {
                const auto global=position+area.definition.origin;
                const auto room=ports_.areas.roomAtPosition(object.area,object.position,int(global.x),int(global.y));
                if(!room) return {DomainStatus::Unavailable,{}};
                auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
                const auto &rule=prototype->second.rule;
                auto identity=prototype->second.identity;identity.spawnKey="quest.tower.controller";
                const auto local=global-ports_.areas.at(room->area).definition.origin;
                monsters::Admission admission{identity,prototype->second.implementation,room->area,local,true,rule,{},tick.tick,true};
                admission.uncounted=true;
                const auto prepared=ports_.monsters.prepareRoomSpawn(
                    admission,
                    *loan.value,-1,{},false,ports_.spatial.nativeCollisionSample());
                if(!prepared) return {prepared.status,{}};
                auto &publication=**prepared.value;
                if(!publication.validate()) return {DomainStatus::Stale,{}};
                const auto actor=publication.actor();publication.publish();
                return actor?DomainResult<EntityId>{DomainStatus::Applied,actor}:DomainResult<EntityId>{DomainStatus::Unavailable,{}};
            };
            auto result=attempt(towerFallback_?object.position+Vec{5,5}:*towerDeathPosition_);
            if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return StepStatus::Blocked;
            if(!result && !towerFallback_) {towerFallback_=true;continue;}
            if(!result) {++towerProbe_;towerFallback_=false;continue;}
            towerController_=*result.value;towerStarted_=true;
            for(size_t index=towerProbe_;index<towerChests_.size();++index) {
                const auto chest=towerChests_[index];
                const auto entry=state_.objects.find(chest);
                if(entry!=state_.objects.end() && ports_.areas.loaded(entry->second.area,entry->second.position)) towerBatch_.push_back(chest);
            }
            break;
        }
        if(!towerStarted_) {towerRetryAt_=tick.tick+10;towerProbe_=0;towerFallback_=false;return StepStatus::Complete;}
    }
    const auto definition=ports_.missiles.nativeDefinition("towerchestspawner");
    if(!definition) throw std::logic_error("Missing native tower chest missile");
    std::vector<missiles::NativeSpawn> requests;
    for(const auto id:towerBatch_) {
        const auto &object=state_.objects.at(id);
        const auto &area=ports_.areas.at(object.area);
        requests.push_back({towerController_,object.area,area.generation,tick.tick,
            object.position,object.position,*definition,0,1,{int32_t(uint32_t(id.value)),0}});
    }
    if(requests.empty()) {towerFinished_=true;return StepStatus::Complete;}
    const auto prepared=ports_.missiles.prepareNativeBirth(requests);
    if(!prepared) {
        if(isDeferred(prepared.status) || prepared.status==DomainStatus::Conflict || prepared.status==DomainStatus::Stale) return StepStatus::Blocked;
        throw std::logic_error("Unable to prepare native tower missiles");
    }
    auto &publication=**prepared.value;
    if(!publication.validate()) return StepStatus::Blocked;
    std::vector<EventBatch> events;
    for(const auto &fact:publication.facts())
        events.push_back({0,tick.tick,{}, {AudienceKind::Area,{},std::get<MissileFact>(fact).area},{fact}});
    if(!events.empty() && !ports_.events.publishGroup(std::move(events))) return StepStatus::Blocked;
    publication.publish();
    for(const auto &request:requests) ports_.world.retainControllerRoom(request.area,request.position,true);
    towerFinished_=true;
    return StepStatus::Complete;
}
}
