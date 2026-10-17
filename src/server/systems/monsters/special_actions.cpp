#include "system.hpp"
#include "native_birth.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <exception>
namespace d2x::server::monsters {
struct NativeBirthPublication::Data {
    System *system{};
    RegionId area;uint64_t generation{};
    std::optional<RoomRandomLoan> room;
    std::optional<RoomRandomLoan> sourceRoom;
    uint64_t randomExpected{},randomNext{};
    ActorPublication actors;
    ComponentPaletteState palettes;
    EntityId actor;
    std::optional<Vec> position;
    bool published{};
};
NativeBirthPublication::NativeBirthPublication(std::unique_ptr<Data> data):data_(std::move(data)) {}
NativeBirthPublication::~NativeBirthPublication()=default;
EntityId NativeBirthPublication::actor() const {return data_->actor;}
std::optional<Vec> NativeBirthPublication::position() const {return data_->position;}
bool NativeBirthPublication::validate() const {
    const auto &d=*data_;const auto &system=*d.system;
    const auto *area=system.ports_.areas.find(d.area);
    return !d.published && area && area->generation==d.generation &&
        (!d.room || system.ports_.areas.validateRoomRandom(*d.room)) &&
        (!d.sourceRoom || system.ports_.areas.validateRoomRandom(*d.sourceRoom)) &&
        (!d.actor || (system.ports_.random==d.randomExpected && system.validateActors(d.actors)));
}
void NativeBirthPublication::publish() noexcept {
    if(!validate()) std::terminate();
    auto &d=*data_;auto &system=*d.system;
    if(d.sourceRoom) system.ports_.areas.publishRoomRandom(*d.sourceRoom);
    if(d.room) system.ports_.areas.publishRoomRandom(*d.room);
    if(d.actor) {
        system.publishComponents(d.palettes);system.publishActors(d.actors);
        system.ports_.random=d.randomNext;
    }
    d.published=true;
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::prepareLevelSpawn(RegionId region,Vec observer,Vec position,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *area=ports_.areas.find(region);
    if(!area || !collision || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(observer.x) || !std::isfinite(observer.y)) return {DomainStatus::InvalidRequest,{}};
    const auto global=position+area->definition.origin;
    const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
    const auto room=ports_.areas.roomAtPosition(region,observer,x,y);if(!room) return {DomainStatus::Unavailable,{}};
    area=ports_.areas.find(room->area);if(!area) return {DomainStatus::Unavailable,{}};
    auto data=std::make_unique<NativeBirthPublication::Data>();data->system=this;data->area=room->area;data->generation=area->generation;
    const auto finish=[&]()->DomainResult<std::shared_ptr<NativeBirthPublication>> {
        return {DomainStatus::Applied,std::shared_ptr<NativeBirthPublication>(new NativeBirthPublication(std::move(data)))};
    };
    if(area->definition.normalSpawnRoster.empty()) return finish();
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    data->room=*loan.value;
    const auto definition=nativeLevelSpawnClass(area->definition.normalSpawnRoster,data->room->next);
    if(!definition) return finish();
    const auto found=area->definition.nativeMonsterClasses.find(*definition);
    if(found==area->definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const auto &prototype=found->second;const auto &rule=prototype.rule;
    auto identity=prototype.identity;identity.spawnKey="native."+std::to_string(ports_.ids.cursor());
    identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;
    return prepareRoomSpawn({identity,prototype.implementation,room->area,global-area->definition.origin,true,rule,{},tick},
        *data->room,5,{},false,collision);
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::prepareRoomSpawn(const Admission &request,RoomRandomLoan loan,
    int attempts,std::optional<size_t> logicalRegion,bool probe,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    if(!request.rule || !collision || !std::isfinite(request.position.x) || !std::isfinite(request.position.y) ||
        request.area!=loan.room.area || request.identity.spawnKey.empty()) return {DomainStatus::InvalidRequest,{}};
    if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};
    const auto &area=ports_.areas.at(request.area);const auto &roomArea=area.definition;
    const auto global=request.position+roomArea.origin;const auto room=loan.room;
    const auto &rule=*request.rule;
    if(rule.enchantment) return {DomainStatus::NotImplemented,{}};
    if(rule.nativeClass<0 || !rule.stats || !rule.formulas || rule.minimumLife<0 || rule.maximumLife<rule.minimumLife)
        return {DomainStatus::InvalidRequest,{}};
    const auto *bounds=roomArea.activation.room(Vec{float(room.x*5),float(room.y*5)}-roomArea.origin);
    if(!bounds) return {DomainStatus::Unavailable,{}};
    int left=int(roomArea.origin.x)+bounds->x,top=int(roomArea.origin.y)+bounds->y;
    int right=left+bounds->width,bottom=top+bounds->height;
    const RoomPopulationLayout *layout=nullptr;int logicalIndex=0;
    if(logicalRegion) {
        const auto roomIndex=size_t(bounds-roomArea.activation.rooms().data());
        if(roomIndex>=roomArea.populationRooms.size()) return {DomainStatus::InvalidRequest,{}};
        layout=&roomArea.populationRooms[roomIndex];
        if(*logicalRegion>=layout->regions.size()) return {DomainStatus::InvalidRequest,{}};
        const auto &region=layout->regions[*logicalRegion];logicalIndex=region.index;
        left=region.left*5;top=region.top*5;right=region.right*5;bottom=region.bottom*5;
    }
    auto data=std::make_unique<NativeBirthPublication::Data>();data->system=this;data->area=request.area;
    data->generation=area.generation;data->room=loan;
    const auto finish=[&]()->DomainResult<std::shared_ptr<NativeBirthPublication>> {
        return {DomainStatus::Applied,std::shared_ptr<NativeBirthPublication>(new NativeBirthPublication(std::move(data)))};
    };
    const auto clear=[&](int column,int row) {
        if(column<left || row<top || column>=right || row>=bottom) return false;
        if(layout) {
            const auto tileColumn=column/5-layout->x,tileRow=row/5-layout->y;
            if(tileColumn<0 || tileRow<0 || tileColumn>layout->width || tileRow>layout->height) return false;
            const auto index=layout->single?0:layout->cells.at(size_t(tileRow)*(layout->width+1)+tileColumn);
            if(layout->regions.at(index).index!=logicalIndex) return false;
        }
        if(rule.spawnAuxiliary) {
            const auto &[offset,mask]=*rule.spawnAuxiliary;
            const int atX=column+int(offset.x),atY=row+int(offset.y);
            const auto adjacent=ports_.areas.roomAtPosition(room.area,Vec{float(room.x*5),float(room.y*5)}-roomArea.origin,atX,atY);
            if(!adjacent || ports_.areas.nativeCollisionMask(*adjacent,atX,atY,mask.size,mask.mask,collision)) return false;
        }
        return request.ignoreSpawnCollision || !ports_.areas.nativeCollisionMask(room,column,row,rule.spawnCollision.size,rule.spawnCollision.mask,collision);
    };
    std::optional<std::pair<int,int>> point;
    if(rule.nativeSpawnFloor) {
        const auto roomIndex=size_t(bounds-roomArea.activation.rooms().data());
        if(roomIndex>=roomArea.populationRooms.size()) return {DomainStatus::NotImplemented,{}};
        const auto &floors=roomArea.populationRooms[roomIndex].floors;
        if(floors.empty()) return finish();
        if(floors.size()==1) return {DomainStatus::NotImplemented,{}};
        size_t index=std::max(size_t(limitedRandom(data->room->next,uint32_t(floors.size()))),size_t{1});
        const auto stop=index-1;
        constexpr std::array<std::pair<int,int>,4> banks{{{0,-3},{3,0},{0,3},{-3,0}}};
        do {
            const auto &floor=floors[index];
            const int column=floor.x*5+3,row=floor.y*5+3;
            if((floor.material&2) && !ports_.areas.nativeCollisionMask(room,column,row,1,0x100,collision))
                for(const auto &[offsetX,offsetY]:banks)
                    if(!ports_.areas.nativeCollisionMask(room,column+offsetX,row+offsetY,2,0x1c09,collision)) {
                        point=std::pair{column,row};break;
                    }
            if(point) break;
            index=(index+1)%floors.size();
        } while(index!=stop);
    } else point=nativeMonsterRingPosition(int(std::floor(global.x)),int(std::floor(global.y)),attempts,data->room->next,clear);
    if(!point || !point->first || !point->second) return finish();
    data->position=Vec{float(point->first)+.5f,float(point->second)+.5f}-roomArea.origin;
    if(probe) return finish();
    if(state_.actors.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto admission=request;admission.position=*data->position;
    if(rule.nativeSpawnFloor) {
        const auto destination=ports_.areas.roomAtPosition(request.area,Vec{float(room.x*5),float(room.y*5)}-roomArea.origin,point->first,point->second);
        if(!destination) return finish();
        if(destination->area!=request.area) return {DomainStatus::NotImplemented,{}};
    }
    data->randomExpected=data->randomNext=ports_.random;data->actor=EntityId{ports_.ids.cursor()};
    auto prepared=prepareAdmissionActor(admission,data->actor,data->randomNext,data->palettes);
    if(!prepared) return {prepared.status,{}};
    auto actor=std::move(*prepared.value);
    const auto allocated=1+actor.birthEquipmentItems;
    if(request.wanderTarget) {
        if(state_.nextWanderTargetOrder==UINT64_MAX) return {DomainStatus::Capacity,{}};
        actor.wanderTargetOrder=state_.nextWanderTargetOrder;
    }
    if(request.ownsMinions) {
        actor.nativeMinionOwner=actor.id;actor.ownsNativeMinions=true;
        actor.transferMinionsOnDeath=request.transferMinions;
    }
    if(request.minionOwner) actor.nativeMinionOwner=request.minionOwner;
    std::map<EntityId,Actor> actors;actors.emplace(data->actor,std::move(actor));
    if(request.minionOwner) {
        const auto *owner=find(request.minionOwner);
        if(!owner || owner->area!=request.area) return {DomainStatus::Stale,{}};
        if(owner->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto candidate=*owner;candidate.nativeMinions.insert(candidate.nativeMinions.begin(),data->actor);++candidate.revision;
        actors.emplace(candidate.id,std::move(candidate));
    }
    data->actors=prepareActorPublication(std::move(actors),allocated);
    return finish();
}
DomainResult<std::optional<Vec>> System::searchRoomSpawn(const Admission &request,RoomRandomLoan &loan,int attempts,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    auto searched=prepareRoomSpawn(request,loan,attempts,{},true,collision);
    if(!searched) return {searched.status,{}};
    const auto &data=*(*searched.value)->data_;
    loan=*data.room;
    return {DomainStatus::Applied,std::optional<std::optional<Vec>>{std::in_place,data.position}};
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::prepareTrapController(const Admission &request,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *area=ports_.areas.find(request.area);
    if(!area || !collision || !request.rule || !std::isfinite(request.position.x) || !std::isfinite(request.position.y))
        return {DomainStatus::InvalidRequest,{}};
    const auto global=request.position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(request.area,request.position,int(global.x),int(global.y));
    if(!room) return {DomainStatus::Unavailable,{}};
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    auto initial=request;initial.ignoreSpawnCollision=true;initial.uncounted=true;
    auto first=prepareRoomSpawn(initial,*loan.value,-1,{},false,collision);
    if(!first || (*first.value)->actor()) return first;
    *loan.value=*(*first.value)->data_->room;
    const auto point=ports_.areas.nativeRestoreCoordinates(*room,int(global.x),int(global.y),0,0x3f11,collision);
    if(!point) return first;
    auto destination=*loan.value;
    if(point->room!=*room) {
        const auto next=ports_.areas.roomRandom(point->room);if(!next) return {next.status,{}};
        destination=*next.value;
    }
    auto fallback=request;fallback.area=point->room.area;fallback.position=point->position;fallback.ignoreSpawnCollision=false;fallback.uncounted=true;
    auto result=prepareRoomSpawn(fallback,destination,3,{},false,collision);
    if(result && point->room!=*room) (*result.value)->data_->sourceRoom=*loan.value;
    return result;
}
DomainResult<EntityId> System::stageRoomSpawn(const Admission &request,RoomRandomLoan &loan,int attempts,EntityIds &ids,
    uint64_t &random,NativeBirthBatch &batch,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto searched=prepareRoomSpawn(request,loan,attempts,{},true,collision);
    if(!searched) return {searched.status,{}};
    auto &search=*(*searched.value)->data_;
    loan=*search.room;
    if(!search.position) return {DomainStatus::Applied,EntityId{}};
    if(state_.actors.size()+batch.actors.size()>=65536 || ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto admission=request;admission.position=*search.position;
    const auto id=ids.allocate();
    auto prepared=prepareAdmissionActor(admission,id,random,batch.palettes,ids.cursor(),&batch.limitedUniques);
    if(!prepared) return {prepared.status,{}};
    auto actor=std::move(*prepared.value);
    for(size_t index=0;index<actor.birthEquipmentItems;++index) ids.allocate();
    batch.limitedUniques.insert(actor.birthLimitedUniques.begin(),actor.birthLimitedUniques.end());
    batch.actors.emplace(id,std::move(actor));
    return {DomainStatus::Applied,id};
}
void System::publishBirthComponents(NativeBirthBatch &batch) noexcept {publishComponents(batch.palettes);}
DomainResult<> System::compressRoom(RegionId region,size_t index,uint64_t tick) {
    if(modifierPending_ || restoring_) return {DomainStatus::Conflict,{}};
    const auto &area=ports_.areas.at(region).definition;
    const auto &bounds=area.activation.rooms().at(index);
    const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
    const auto order=ports_.areas.roomUnitOrder(room);
    for(const auto id:order) {
        const auto *unit=find(id);if(!unit) continue;
        if(unit->owner || !unit->admissionRule || unit->busyUntil>tick || unit->skillMotion || unit->poison || unit->wound ||
            (!unit->alive() && !unit->rewardComplete)) return {DomainStatus::Unavailable,{}};
        if(unit->revision>=UINT64_MAX-2) return {DomainStatus::Capacity,{}};
        if(unit->questNpc || unit->rule.boss || unit->rule.nativeSpawnFloor || unit->identity.rank!=MonsterRank::Normal ||
            unit->identity.spawnKey.starts_with("quest.") || unit->specialTargetOrder || unit->wanderTargetOrder)
            return {DomainStatus::NotImplemented,{}};
    }
    auto loan=ports_.areas.roomRandom(room);if(!loan) return {loan.status,{}};
    std::vector<std::pair<EntityId,bool>> decisions;
    effects::AuraUnitPublication removal;
    for(const auto id:order) {
        const auto found=state_.actors.find(id);if(found==state_.actors.end()) continue;
        const auto &unit=found->second;
        const bool dead=!unit.alive();
        bool compress=area.saveMonsters || unit.restoredFromInactive;
        if(dead && tick>=unit.busyUntil && limitedRandom(loan.value->next,3)!=0) compress=false;
        if(dead) {
            if(unit.alignment()==UnitAlignment::Good || unit.identity.rank==MonsterRank::Unique || unit.identity.rank==MonsterRank::Champion ||
                unit.identity.rank==MonsterRank::SuperUnique || unit.identity.rank==MonsterRank::Minion) compress=false;
            if(unit.rule.states) for(const auto state:ports_.effects.unitStates(id,tick))
                if(unit.rule.states->at(state).corpseUnselectable) compress=false;
        }
        if(unit.rule.restore==0) compress=false;
        else if(unit.rule.restore==2) compress=true;
        decisions.emplace_back(id,compress);
        if(compress) {
            const auto prepared=ports_.effects.prepareUnitCurseRemoval(removal,id,tick);
            if(!prepared) return prepared;
        }
    }
    if(!ports_.effects.validateAuraUnits(removal)) return {DomainStatus::Stale,{}};
    if(!removal.facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(removal.facts)});
        if(!sent) return {sent.status,{}};
    }
    ports_.effects.publishAuraUnits(removal);
    ports_.areas.publishRoomRandom(*loan.value);
    auto &saved=state_.inactive[{region,index}];
    for(const auto &[id,compress]:decisions) {
        const auto found=state_.actors.find(id);
        const auto &unit=found->second;
        const bool dead=!unit.alive();
        if(compress) {
            InactiveMonster node;
            node.id=id;node.identity=unit.identity;node.implementation=unit.implementation;
            node.area=region;node.position=unit.position;node.admissionRule=unit.admissionRule;
            node.frame=tick;node.revision=unit.revision;node.life=unit.life;node.maximumLife=unit.maximumLife;
            node.corpse=unit.corpse;node.selfResurrections=unit.selfResurrections;
            if(unit.nativeStats && unit.rule.stats) {
                const auto experience=unit.rule.stats->names.find("experience");
                if(experience!=unit.rule.stats->names.end()) {
                    const auto values=ports_.effects.unitStatValues(id,tick);
                    if(!values) throw std::logic_error("Missing native inactive monster stats");
                    node.experience=values->get({experience->second,0});
                }
            }
            node.alignment=int(unit.alignment());node.hostile=unit.hostile;node.dead=dead;node.resurrected=unit.restoredFromInactive;
            node.noTreasure=unit.rule.noTreasure;node.noExperience=unit.rule.noExperience;
            node.uncounted=unit.uncounted;
            node.ownsMinions=unit.ownsNativeMinions;node.transferMinions=unit.transferMinionsOnDeath;
            node.minionOwner=unit.nativeMinionOwner;node.minions=unit.nativeMinions;
            node.wander=unit.wanderTargetOrder!=0;node.special=unit.specialTargetOrder!=0;
            node.modifiers=unit.rule.nativeModifiers;node.nameSeed=unit.rule.enchantment?unit.rule.enchantment->nameSeed:0;
            node.skillPositions=unit.skillPositions;node.componentParameter=unit.components[10];node.nestSpawned=unit.nestSpawned;
            saved.insert(saved.begin(),std::move(node));
        }
        ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
        ports_.areas.removeRoomUnit(id);state_.actors.erase(found);
    }
    if(saved.empty()) state_.inactive.erase({region,index});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::finishRestoration(const InactiveMonster &source,uint64_t tick) {
    const auto *unit=find(source.id);if(!unit) return {DomainStatus::Stale,{}};
    if(unit->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto candidate=*unit;
    attributes::MonsterResourceTrace resources(candidate,tick);
    const auto sources=ports_.effects.unitStatSources(source.id,tick);
    resources.sampleSources(sources);
    resources.writeBaseStat("maxhp",nativeNarrow(source.maximumLife),sources);
    resources.writeBaseStat("alignment",source.alignment,sources);
    resources.writeBaseStat("experience",source.experience,sources);
    resources.seedLife(Fixed8::fromRaw(nativeNarrow(source.dead?0:uint32_t(tick-source.frame)<7500?source.life:source.maximumLife)));
    auto life=resources.take();candidate.nativeStats=std::move(life.intrinsic);
    candidate.life=life.life.raw();candidate.maximumLife=life.maximum.raw();candidate.modeAlive=!source.dead && candidate.life==0;
    candidate.nativeMinionOwner=source.minionOwner;candidate.nativeMinions=source.minions;
    candidate.ownsNativeMinions=source.ownsMinions;candidate.transferMinionsOnDeath=source.transferMinions;
    candidate.rule.noTreasure=source.noTreasure;candidate.rule.noExperience=source.noExperience;
    candidate.restoredFromInactive=true;
    candidate.corpse=source.corpse;candidate.selfResurrections=source.selfResurrections;
    candidate.uncounted=source.uncounted;
    candidate.identity=source.identity;
    if(source.dead) {candidate.deathTick=tick;candidate.rewardComplete=true;}
    if(source.admissionRule->sharedRandomComponents) {
        candidate.components[10]=uint8_t(source.componentParameter);
        if(source.admissionRule->sharedRandomComponents&(1u<<11)) candidate.components[11]=uint8_t(source.componentParameter);
    }
    if(candidate.rule.ai.kind==MonsterAiKind::SandMaggot) candidate.nestSpawned=source.nestSpawned;
    if(source.wander) {
        if(state_.nextWanderTargetOrder==UINT64_MAX) return {DomainStatus::Capacity,{}};
        candidate.wanderTargetOrder=state_.nextWanderTargetOrder;
    }
    if(source.special) {
        if(state_.nextSpecialTargetOrder==UINT64_MAX) return {DomainStatus::Capacity,{}};
        candidate.specialTargetOrder=state_.nextSpecialTargetOrder;
    }
    ++candidate.revision;
    auto publication=prepareActorPublication({{source.id,std::move(candidate)}},0);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishActors(publication);
    if(source.wander) ++state_.nextWanderTargetOrder;
    if(source.special) ++state_.nextSpecialTargetOrder;
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::restoreRoom(RegionId region,size_t index,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision,bool sanctumCleared) {
    if(restoring_ && modifierPending_) return StepStatus::Blocked;
    if(restoring_) {
        for(auto entry=state_.inactive.begin();entry!=state_.inactive.end();++entry) {
            auto &nodes=entry->second;
            if(nodes.empty() || nodes.front().id!=restoring_->id) continue;
            const auto finished=finishRestoration(nodes.front(),tick);
            if(!finished) return isDeferred(finished.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            restoring_.reset();nodes.erase(nodes.begin());
            if(nodes.empty()) state_.inactive.erase(entry);
            return StepStatus::Blocked;
        }
        return StepStatus::NotImplemented;
    }
    const auto found=state_.inactive.find({region,index});if(found==state_.inactive.end()) return StepStatus::Complete;
    auto &saved=found->second;
    const auto &area=ports_.areas.at(region).definition;const auto &bounds=area.activation.rooms().at(index);
    const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
    while(!saved.empty()) {
        auto &source=saved.front();
        if(int(region)==108 && sanctumCleared && !source.dead && source.identity.monster!="diablo") {
            saved.erase(saved.begin());continue;
        }
        if(find(source.id)) {
            if(modifierPending_) return StepStatus::Blocked;
            if(source.identity.rank==MonsterRank::Unique || source.identity.rank==MonsterRank::Champion || source.identity.rank==MonsterRank::SuperUnique || source.identity.rank==MonsterRank::Minion) {
                if(!find(source.id)->rule.enchantment) {
                    const auto modifiers=prepareBirthModifiers(source.id,source.identity.rank,source.modifiers,{},tick,{},false,source.nameSeed);
                    if(!modifiers) return isDeferred(modifiers.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                    restoring_=source;return StepStatus::Blocked;
                }
            }
            const auto finished=finishRestoration(source,tick);
            if(!finished) return isDeferred(finished.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            saved.erase(saved.begin());
            if(!saved.empty()) return StepStatus::Blocked;
            continue;
        }
        if(modifierPending_) return StepStatus::Blocked;
        if(state_.actors.size()>=65536 || source.revision>=UINT64_MAX-1) return StepStatus::Blocked;
        auto identity=source.identity;identity.rank=source.admissionRule->boss?MonsterRank::Boss:MonsterRank::Normal;
        Admission request{identity,source.implementation,region,source.position,source.hostile,*source.admissionRule,source.skillPositions,tick,source.dead};
        request.ignoreSpawnCollision=source.admissionRule->flying;
        request.uncounted=source.uncounted;
        auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
        std::optional<RoomRandomLoan> sourceLoan;
        std::optional<Vec> point;
        for(const int attempts:{-1,source.identity.rank==MonsterRank::Minion?4:5}) {
            const auto searched=prepareRoomSpawn(request,*loan.value,attempts,{},true,collision);
            if(!searched) return isDeferred(searched.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            auto &search=*(*searched.value)->data_;*loan.value=*search.room;
            if(search.position) {point=search.position;break;}
        }
        if(!point) {
            const int left=room.x*5+1,top=room.y*5+1;
            for(int attempt=0;attempt<20 && !point;++attempt) {
                request.position=Vec{float(left+int(limitedRandom(loan.value->next,uint32_t(bounds.width-1)))),
                    float(top+int(limitedRandom(loan.value->next,uint32_t(bounds.height-1))))}-area.origin;
                auto candidate=request;candidate.ignoreSpawnCollision=false;
                const auto probe=prepareRoomSpawn(candidate,*loan.value,-1,{},true,collision);
                if(!probe) return isDeferred(probe.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                auto &search=*(*probe.value)->data_;*loan.value=*search.room;
                if(!search.position) continue;
                const auto birth=prepareRoomSpawn(request,*loan.value,-1,{},true,collision);
                if(!birth) return isDeferred(birth.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                auto &actual=*(*birth.value)->data_;*loan.value=*actual.room;
                point=actual.position;
                break;
            }
        }
        if(!point) {
            const auto global=source.position+area.origin;
            const auto free=ports_.areas.nativeRestoreCoordinates(room,int(global.x),int(global.y),1,0x3c01,collision);
            if(free) {
                if(free->room!=loan.value->room) {
                    auto destination=ports_.areas.roomRandom(free->room);
                    if(!destination) return StepStatus::NotImplemented;
                    sourceLoan=*loan.value;
                    *loan.value=*destination.value;
                }
                request.area=free->room.area;
                request.position=free->position;
                const auto searched=prepareRoomSpawn(request,*loan.value,-1,{},true,collision);
                if(!searched) return isDeferred(searched.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                auto &search=*(*searched.value)->data_;*loan.value=*search.room;point=search.position;
            }
        }
        const auto publishSearch=[&] {
            if(sourceLoan) ports_.areas.publishRoomRandom(*sourceLoan);
            ports_.areas.publishRoomRandom(*loan.value);
        };
        if(!point) {publishSearch();saved.erase(saved.begin());continue;}
        request.position=*point;
        auto random=ports_.random;ComponentPaletteState palettes;
        auto prepared=prepareAdmissionActor(request,source.id,random,palettes,ports_.ids.cursor());
        if(!prepared) return isDeferred(prepared.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        auto actor=std::move(*prepared.value);
        const auto allocated=actor.birthEquipmentItems;
        actor.revision=source.revision+1;
        actor.admissionRevision=actor.revision;
        const std::array restored{source.id};
        auto publication=prepareActorPublication({{source.id,std::move(actor)}},allocated,restored);
        if(!validateActors(publication)) return StepStatus::Blocked;
        if(!ports_.areas.validateRoomRandom(*loan.value) || (sourceLoan && !ports_.areas.validateRoomRandom(*sourceLoan)))
            return StepStatus::Blocked;
        publishSearch();
        publishComponents(palettes);publishActors(publication);ports_.random=random;
        if(source.identity.rank==MonsterRank::Unique || source.identity.rank==MonsterRank::Champion || source.identity.rank==MonsterRank::SuperUnique || source.identity.rank==MonsterRank::Minion) {
            const auto modifiers=prepareBirthModifiers(source.id,source.identity.rank,source.modifiers,{},tick,{},false,source.nameSeed);
            if(!modifiers) return isDeferred(modifiers.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            restoring_=source;return StepStatus::Blocked;
        }
        const auto finished=finishRestoration(source,tick);
        if(!finished) return isDeferred(finished.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        saved.erase(saved.begin());
        if(!saved.empty()) return StepStatus::Blocked;
    }
    state_.inactive.erase(found);return StepStatus::Complete;
}
DomainResult<> System::finishSuperUniqueBirth(EntityId id,bool corpseUnavailable) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || found->second.identity.rank!=MonsterRank::SuperUnique) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    if(actor.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(actor.rule.nativeModifiers.size()<9) {
        actor.rule.nativeModifiers.push_back(22);
        if(actor.rule.enchantment) actor.rule.enchantment->ids.push_back(22);
    }
    actor.corpse.unavailable|=corpseUnavailable;++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
void System::disableTreasure(EntityId id) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || found->second.rule.noTreasure) return;
    if(found->second.revision==UINT64_MAX) throw std::overflow_error("Native target flag revision exhausted");
    found->second.rule.noTreasure=true;++found->second.revision;
}
void System::drainResources(EntityId id,int32_t mana,int32_t stamina,uint64_t tick) {
    if(mana<=0 && stamina<=0) return;
    const auto *unit=find(id);if(!unit) return;
    attributes::MonsterResourceTrace resources(*unit,tick);
    const auto sources=ports_.effects.unitStatSources(id,tick);
    resources.sampleSources(sources);
    const auto drain=[&](std::string_view name,int32_t amount) {
        if(amount<=0) return;
        const auto values=resources.values(sources);
        const auto current=values.get({unit->rule.stats->names.at(std::string(name)),0});
        resources.writeBaseStat(name,nativeChecked(subtractContactResource(current,amount)),sources);
    };
    drain("mana",mana);drain("stamina",stamina);
    if(!commitLife(resources.take())) throw std::logic_error("Native monster resource drain changed during HP commit");
}
bool System::deadMode(EntityId id,uint64_t tick) const {
    const auto *unit=find(id);return unit && !unit->alive() && tick>=unit->busyUntil;
}
bool System::selectableCorpse(EntityId id,uint64_t tick) const {
    const auto *unit=find(id);
    if(!unit || !deadMode(id,tick) || !unit->rule.corpseSelectable || unit->corpse.unavailable || unit->corpse.searched) return false;
    if(unit->rule.states) for(const auto state:ports_.effects.unitStates(id,tick)) {
        const auto definition=unit->rule.states->find(state);
        if(definition!=unit->rule.states->end() && definition->second.corpseUnselectable) return false;
    }
    return true;
}
bool System::nativeUsableCorpse(EntityId id,uint64_t tick) const {
    const auto *unit=find(id);
    if(!unit || !deadMode(id,tick) || !unit->rule.corpseSelectable || unit->rule.deadCollision) return false;
    if(unit->rule.states) for(const auto state:ports_.effects.unitStates(id,tick)) {
        const auto definition=unit->rule.states->find(state);
        if(definition!=unit->rule.states->end() && definition->second.corpseUnselectable) return false;
    }
    return true;
}

bool System::usableCorpse(EntityId id,RegionId area,uint64_t tick,bool revive) const {
    const auto *dead=find(id);
    return dead && dead->area==area && !dead->owner && selectableCorpse(id,tick) &&
        (!revive || (dead->rule.revivable && dead->rule.switchAi && dead->identity.rank==MonsterRank::Normal && !dead->rule.boss && !dead->rule.primeEvil));
}
bool System::consumableCorpse(EntityId id,RegionId area,uint64_t tick,bool revive) const {
    return usableCorpse(id,area,tick,revive) && find(id)->rule.nativeVelocity!=0;
}
void System::consumeCorpse(EntityId id) noexcept {
    if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.corpse.unavailable=true;++it->second.revision;}
}
DomainResult<> System::lightningEmission(EntityId id,bool emitted,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    // MonsterMsg carries nLastAnimMode bit 0 in the high life bit. It is
    // refreshed even when HP is unchanged; no private missile notification.
    auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},
        {HitFact{id,1,actor.area,monsterLifeRatio(actor.life,actor.maximumLife),false,actor.position,0,0,{},emitted}}});
    if(!result) return {result.status,{}};
    actor.lightningReady=emitted;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::introduction(EntityId id,uint64_t tick) {
    const auto *actor=find(id);if(!actor || actor->life<=0) return {DomainStatus::InvalidActor,{}};
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor->area},{SoundFact{id,1,actor->area,16}}})) return {DomainStatus::Capacity,{}};
    stop(id);return beginAttack(id,tick+20,true);
}
bool System::resurrectionTarget(EntityId source,EntityId corpse,uint64_t tick) const {
    const auto *caster=find(source),*dead=find(corpse);
    if(!caster || !dead || source==corpse || caster->life<=0 || dead->life>0 || dead->owner || !dead->rule.corpseSelectable ||
        dead->corpse.unavailable || dead->corpse.searched || tick<dead->busyUntil || !dead->rewardComplete || dead->area!=caster->area) return false;
    if(caster->rule.ai.kind==MonsterAiKind::GreaterMummy) return mummySupportTarget(source,corpse,tick,true);
    const int radius=caster->rule.ai.params[3];
    const bool unique=nativeUniqueMonster(caster->identity.rank);
    if(unique) {
        // Fn013's unique callback accepts both Fallen and FallenShaman families,
        // excluding unique/champion corpses, with a squared coordinate metric.
        if(!dead->enemyTarget() || dead->alignment()!=UnitAlignment::Evil || (dead->rule.ai.kind!=MonsterAiKind::Fallen && dead->rule.ai.kind!=MonsterAiKind::FallenShaman) ||
            (dead->identity.rank!=MonsterRank::Normal && dead->identity.rank!=MonsterRank::Minion)) return false;
        const Vec delta{std::floor(caster->position.x)-std::floor(dead->position.x),
            std::floor(caster->position.y)-std::floor(dead->position.y)};
        if(delta.x*delta.x+delta.y*delta.y>radius*radius) return false;
    } else if(minionOwner(dead->id)!=caster->id) return false;
    // Fn013 deliberately passes a squared threshold to callback 9, whose
    // actual metric is FullUnitSize. Preserve that native quirk and room scope.
    return ports_.areas.at(caster->area).definition.activation.nearby(caster->position,dead->position) &&
        (unique || monsterAiDistance(caster->position,caster->rule.size,dead->position)<=radius*radius);
}
DomainResult<> System::resurrect(EntityId source,EntityId corpse,uint64_t tick) {
    if(!resurrectionTarget(source,corpse,tick)) return {DomainStatus::Unavailable,{}};
    auto &actor=state_.actors.at(corpse);const auto &grid=ports_.areas.at(actor.area).definition.collision;
    if(!grid.walkable(actor.position,actor.rule.collision)) return {DomainStatus::Unavailable,{}};
    for(const auto &[id,other]:state_.actors) if(id!=corpse && other.area==actor.area && other.life>0 && (actor.position-other.position).length()<1.f) return {DomainStatus::Unavailable,{}};
    for(const auto &[id,p]:ports_.players.all()) { (void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && (actor.position-p.position).length()<1.f) return {DomainStatus::Unavailable,{}}; }
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{HitFact{corpse,1,actor.area,128,false,actor.position}}})) return {DomainStatus::Capacity,{}};
    actor.life=actor.maximumLife;actor.chilledUntil=actor.frozenUntil=0;actor.shatterActive=false;actor.corpse={};actor.poison.reset();
    actor.riseUntil=actor.busyUntil=tick+uint64_t(actor.rule.resurrectionTicks);actor.riseMode=actor.rule.resurrectionMode;
    actor.neutralAt=actor.riseUntil;
    actor.killer={};actor.route.clear();actor.moving=actor.running=false;actor.movementTarget={};
    actor.hitOccurrence=0;actor.knockedUntil=0;actor.knockbackGoal.reset();++actor.revision;
    scheduleAi(corpse,actor.riseUntil);
    // rewardComplete remains set: SKILLS_ResurrectUnit gives NOXP | NOTC.
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<EntityId> System::spawnNestChild(EntityId id,uint64_t tick) {
    const auto *source=find(id);if(!source) return {DomainStatus::InvalidActor,{}};
    return spawnNestChild(id,tick,source->position+source->rule.spawnOffset);
}
DomainResult<EntityId> System::spawnNestChild(EntityId id,uint64_t tick,Vec origin) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || !it->second.rule.nestChild) return {DomainStatus::InvalidActor,{}};
    auto &nest=it->second;const auto &child=*nest.rule.nestChild;
    if(nest.nestSpawned>=(nest.rule.ai.kind==MonsterAiKind::BloodRaven?8+2*nest.rule.difficulty:nest.rule.ai.params[2]+(nest.rule.ai.kind==MonsterAiKind::Sarcophagus?1:0))) return {DomainStatus::Unavailable,{}};
    auto random=nest.combatRandom;
    const auto point=nestBirthPoint(id,origin,random);
    if(!point) {nest.combatRandom=random;return {DomainStatus::Unavailable,{}};}
    auto identity=child.identity;identity.spawnKey=nest.identity.spawnKey+".child."+std::to_string(nest.nestSpawned);identity.ownerSpawnKey=nest.identity.spawnKey;
    const auto admitted=admit({identity,child.implementation,nest.area,*point,true,child.rule,{},tick});
    if(admitted) {state_.actors.at(*admitted.value).rewardComplete=true;++nest.nestSpawned;nest.combatRandom=random;++nest.revision;}
    (void)tick;return admitted;
}
bool System::canSpawnNestChild(EntityId id) const {
    const auto *nest=find(id);if(!nest || nest->life<=0 || !nest->rule.nestChild) return false;
    auto random=nest->combatRandom;
    return bool(nestBirthPoint(id,nest->position+nest->rule.spawnOffset,random));
}
std::optional<Vec> System::nestBirthPoint(EntityId id,Vec origin,uint64_t &random) const {
    const auto *source=find(id);if(!source || source->life<=0 || !source->rule.nestChild) return {};
    const auto &nest=*source;const auto &child=*nest.rule.nestChild;
    const auto &grid=ports_.areas.at(nest.area).definition.collision;
    const int radius=nest.rule.ai.kind==MonsterAiKind::BloodRaven?1:3;
    // master nestSpawn's perimeter walk; MPQ spawnx/spawny supply the origin.
    int x=0,y=0;
    if(rollRandom(random)&1) {y=radius;x=int(limitedRandom(random,unsigned(radius)));}else {x=radius;y=int(limitedRandom(random,unsigned(radius)));}
    if(rollRandom(random)&1) x=-x;
    if(rollRandom(random)&1) y=-y;
    for(int attempt=0;attempt<8*radius;++attempt) {
        const Vec point=origin+Vec{float(x),float(y)};
        if(x==-radius && y<radius) ++y;else if(y==radius && x<radius) ++x;else if(x==radius && y>-radius) --y;else --x;
        if(!grid.walkable(point,child.rule.spawnCollision)) continue;
        bool occupied=false;
        for(const auto &[key,actor]:state_.actors) { (void)key;if(actor.area==nest.area && actor.life>0 && (point-actor.position).length()<1.f) {occupied=true;break;} }
        for(const auto &[key,p]:ports_.players.all()) { (void)key;if(p.entered && p.area==nest.area && p.persistent.player.hp>0 && (point-p.position).length()<1.f) {occupied=true;break;} }
        if(occupied) continue;
        return point;
    }
    return {};
}
DomainResult<> System::activateWeb(EntityId id,int skill,int rank,uint64_t tick) {
    const auto *live=find(id);if(!live || !live->alive() || !live->rule.web || !live->rule.stats || !live->rule.formulas) return {DomainStatus::InvalidActor,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto sampled=ports_.effects.unitStatValues(id,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
    auto actor=*live;auto random=actor.combatRandom;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));};
    auto input=skills::formulaInput(actor,*sampled,draw);SkillFormulaValues values(*actor.rule.formulas,input);
    const auto length=values.calculation(FormulaDomain::Skill,skill,rank,"auralencalc");if(!length) return {DomainStatus::Unavailable,{}};
    effects::UnitDamageStates update;CombatEffectSpec spec;spec.state=actor.rule.web->aura;
    spec.source={CombatEffectSource::Skill,id,skill,rank};spec.stacking=EffectStacking::SkillLevel;
    spec.nativeState=true;spec.nativeValues.emplace();spec.registersUnitEvents=true;
    for(const auto binding:values.unitEvents(skill)) {if(binding.event<0) break;if(binding.function>0) spec.unitEvents.push_back(binding);}
    if(*length>0) spec.duration=uint64_t(*length);spec.refreshPassivesOnRemoval=true;
    update.statusWrites=values.passiveBuffStats(skill,rank,false);
    update.statusWrites.emplace_back(StatKey{actor.rule.stats->names.at("modifierlist_skill"),0},skill);
    update.statusWrites.emplace_back(StatKey{actor.rule.stats->names.at("modifierlist_level"),0},rank);
    update.status=std::move(spec);
    actor.webUntil=*length>0?tick+uint64_t(*length):UINT64_MAX;actor.combatRandom=random;++actor.revision;
    auto plan=ports_.effects.prepareDamageStates(std::move(actor),std::move(update),tick);
    if(!ports_.effects.validateAuraUnits(plan)) return {DomainStatus::Conflict,{}};
    const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},live->area},std::move(plan.facts)});if(!result) return {result.status,{}};
    ports_.effects.publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::heal(EntityId id,int64_t amount) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || amount<0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;actor.life=std::min(actor.maximumLife,actor.life+amount);++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::healSkill(EntityId source,EntityId target,int64_t amount,uint64_t tick,std::pair<uint64_t,uint64_t> random) {
    const auto *caster=find(source),*patient=find(target);
    if(!caster || !patient || !patient->alive() || !patient->nativeStats || !patient->rule.stats || amount<0) return {DomainStatus::InvalidActor,{}};
    if(caster->combatRandom!=random.first) return {DomainStatus::Stale,{}};
    if(caster->revision==UINT64_MAX || patient->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    attributes::MonsterResourceTrace resources(*patient,tick,ports_.effects.unitEffects(target));
    resources.seedLife(Fixed8::fromRaw(nativeChecked(std::clamp<int64_t>(patient->life+amount,1,patient->maximumLife))));
    auto change=resources.take();
    if(source==target) {change.random=random;change.changed=true;}
    if(!canCommitLife(change)) return {DomainStatus::Conflict,{}};
    if(source!=target) {const auto consumed=consumeRandom(source,random.first,random.second);if(!consumed) return consumed;}
    if(!commitLife(std::move(change))) throw std::logic_error("Monster skill healing lost its resource reservation");
    return {DomainStatus::Applied,std::monostate{}};
}
void System::shortenPoison(EntityId id,uint64_t tick,int remainingPercent) {
    auto it=state_.actors.find(id);
    if(it!=state_.actors.end() && it->second.poison && it->second.poison->until>tick)
        it->second.poison->until=nativeAuraDurationTick(tick,it->second.poison->until,remainingPercent);
}
DomainResult<> System::convert(EntityId id,const ActorContext &actor,const NativeConversionSpec &skill,LifeChange resources,
    std::optional<ConversionResourceSave> saved,std::vector<DomainFact> facts) {
    auto it=state_.actors.find(id);const auto *p=ports_.players.find(actor.player);
    const auto *source=find(actor.actor);const auto origin=source?source->position:p?p->position:Vec{};
    if(it==state_.actors.end() || !p || !p->rules.skills || (skill.procedure==NativeConversionProcedure::Paladin && !it->second.rule.convertible) || it->second.owner || it->second.alignment()!=UnitAlignment::Evil || it->second.life<=0 ||
       it->second.identity.rank==MonsterRank::Unique || it->second.identity.rank==MonsterRank::SuperUnique ||
       (it->second.area!=actor.area && (skill.procedure!=NativeConversionProcedure::MindBlast || !ports_.areas.nearby(actor.area,origin,it->second.area,it->second.position))))
        return {DomainStatus::InvalidRequest,{}};
    if(resources.actor!=id || !resources.intrinsic || !resources.intrinsic->sources || !it->second.rule.stats || !canCommitLife(resources))
        return {DomainStatus::Conflict,{}};
    auto &m=it->second;const int alignment=p->rules.skills->alignment.id,stat=p->rules.skills->nativeStats.at("alignment");
    facts.insert(facts.begin(),StateFact{id,1,m.area,alignment,true,{{stat,2}}});
    facts.insert(facts.begin(),StateFact{id,1,m.area,skill.state.id,true});
    const auto deadline=nativeEventDeadline(actor.tick,nativeAdd(nativeNarrow(uint32_t(actor.tick)),skill.frames));
    auto timer=ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,id},FrameEventType::RemoveState,deadline.due,deadline.continuous,
        effects::conversionTimerHandler,skill.state.id});
    if(!ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},m.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    if(!commitLife(std::move(resources))) throw std::logic_error("Native conversion resource commit lost its prepared revision");
    m.conversion=Actor::Conversion{skill.state.id,alignment,stat,deadline.due,saved,skill.saveState.id,skill.procedure};
    ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,id},FrameEventType::RemoveState,effects::conversionTimerHandler);
    ports_.frameEvents.publish(std::move(timer));
    m.rule.level=logicalStatValue(m.nativeStats->values,*m.rule.stats,"level");
    m.owner=actor.player;++m.allegianceRevision;++m.interruption;stop(id);++m.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::finishConversion(EntityId id,uint64_t tick,LifeChange resources,std::vector<DomainFact> facts) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.conversion) return {DomainStatus::InvalidActor,{}};
    auto &m=found->second;const auto conversion=*m.conversion;
    if(resources.actor!=id || !resources.intrinsic || !resources.intrinsic->sources || !m.rule.stats || !canCommitLife(resources))
        return {DomainStatus::Conflict,{}};
    facts.insert(facts.begin(),StateFact{id,1,m.area,conversion.alignment,true,{{conversion.stat,0}}});
    facts.insert(facts.begin(),StateFact{id,1,m.area,conversion.state,false});
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    if(!commitLife(std::move(resources))) throw std::logic_error("Native conversion removal resource commit lost its prepared revision");
    ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,id},FrameEventType::RemoveState,effects::conversionTimerHandler);
    m.rule.level=logicalStatValue(m.nativeStats->values,*m.rule.stats,"level");
    m.owner.reset();m.conversion.reset();++m.allegianceRevision;++m.interruption;stop(id);++m.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::slow(EntityId id,int state,int percent,uint64_t frames,uint64_t tick,EntityId source) {
    const auto *live=find(id);if(!live || !live->alive() || state<0 || !live->rule.states || !live->rule.states->contains(state) ||
        !live->rule.stats || percent< -100 || percent>0 || !frames) return {DomainStatus::InvalidActor,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto actor=*live;++actor.revision;
    effects::UnitDamageStates update;CombatEffectSpec spec;spec.state=actor.rule.states->at(state);
    spec.source={CombatEffectSource::Monster,source,state,0};spec.stacking=EffectStacking::ReuseState;
    spec.duration=frames;spec.refreshPassivesOnRemoval=true;update.status=std::move(spec);
    update.statusWrites.push_back({actor.rule.stats->names.at("velocitypercent"),percent});
    auto plan=ports_.effects.prepareDamageStates(std::move(actor),std::move(update),tick);
    if(!ports_.effects.validateAuraUnits(plan)) return {DomainStatus::Conflict,{}};
    const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},live->area},std::move(plan.facts)});if(!result) return {result.status,{}};
    ports_.effects.publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::teleport(EntityId id,Vec point,uint64_t tick,int heal,bool nativeFreePosition) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &m=it->second;const auto &grid=ports_.areas.at(m.area).definition.collision;
    if(!grid.walkable(point,{uint16_t(nativeFreePosition?0x1c09:0x3c01),m.rule.size}) ||
       (!nativeFreePosition && !grid.segment(m.position,point,{}, {0x0c01,1}))) return {DomainStatus::Unavailable,{}};
    for(const auto &[key,other]:state_.actors) if(key!=id && other.area==m.area && other.life>0 && meleeDistance(point,m.rule.size,other.position,other.rule.size)<=0) return {DomainStatus::Unavailable,{}};
    for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m.area && p.persistent.player.hp>0 && meleeDistance(point,m.rule.size,p.position,2)<=0) return {DomainStatus::Unavailable,{}};}
    // Native monster relocation is projected by 0x6D snapshot correction.
    (void)tick;stop(id);m.position=point;ports_.areas.moveRoomUnit(id,m.area,m.position);m.life=std::min(m.maximumLife,m.life+int64_t(std::max(0,heal))*256);++m.revision;
    return {DomainStatus::Applied,std::monostate{}};
}

}
