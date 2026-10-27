#include "system.hpp"
#include "server/systems/spatial/system.hpp"
#include "native_birth.hpp"
#include "native_birth_data.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/attributes/monster_sources.hpp"
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
    int attempts,std::optional<size_t> logicalRegion,bool probe,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision,bool stagedAdmission) {
    if(!request.rule || !collision || !std::isfinite(request.position.x) || !std::isfinite(request.position.y) ||
        request.area!=loan.room.area || request.identity.spawnKey.empty()) return {DomainStatus::InvalidRequest,{}};
    if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};
    const auto &area=ports_.areas.at(request.area);const auto &roomArea=area.definition;
    const auto global=request.position+roomArea.origin;const auto room=loan.room;
    const auto &rule=*request.rule;
    if(rule.enchantment) return {DomainStatus::NotImplemented,{}};
    if(rule.nativeClass<0 || !rule.stats || !rule.formulas || rule.minimumLife<0 || rule.maximumLife<0)
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
        if(rule.spawnAuxiliary && (!probe || stagedAdmission || !rule.spawnAuxiliaryOnAdmissionOnly)) {
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
    // Defer allocation to the batch, but retain actual birth collision checks.
    const auto searched=prepareRoomSpawn(request,loan,attempts,{},true,collision,true);
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
        (!revive || (dead->rule.revivable && skillAiControlAllowed(dead->identity.rank,dead->rule.switchAi,dead->rule.boss) &&
            !ports_.effects.unitStates(id,tick).contains(dead->rule.uninterruptableState)));
}
bool System::consumableCorpse(EntityId id,RegionId area,uint64_t tick,bool revive) const {
    return usableCorpse(id,area,tick,revive) && find(id)->rule.nativeVelocity!=0;
}
void System::consumeCorpse(EntityId id) noexcept {
    if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.corpse.unavailable=true;++it->second.revision;}
}
DomainResult<> System::eatCorpse(EntityId source,EntityId target,int skill,int rank,uint64_t tick) {
    const auto *caster=find(source),*corpse=find(target);
    if(!caster || !caster->alive() || !corpse || corpse->hireling || !deadMode(target,tick) || source==target)
        return {DomainStatus::InvalidActor,{}};
    if(caster->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    attributes::MonsterResourceTrace resources(*caster,tick,ports_.effects.unitEffects(source));
    const auto sources=ports_.effects.unitStatSources(source,tick);resources.sampleSources(sources);
    std::optional<std::pair<uint64_t,uint64_t>> seed;
    if(corpse->maximumLife>0) {
        auto random=caster->combatRandom;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=skills::formulaInput(*caster,resources.values(sources),draw);SkillFormulaValues values(*caster->rule.formulas,input);
        const auto percent=values.calculation(FormulaDomain::Skill,skill,rank,"calc1");if(!percent) return {DomainStatus::Unavailable,{}};
        const auto amount=nativePercentage(nativeChecked(corpse->maximumLife),*percent,100);
        const auto life=nativeAdd(resources.read().life.raw(),amount);
        resources.seedLife(Fixed8::fromRaw(std::min(std::max(life,1),resources.read().maximum.raw())));
        if(random!=caster->combatRandom) seed=std::pair{caster->combatRandom,random};
    }
    auto change=resources.take();if(seed) {change.random=seed;change.changed=true;}
    if(!canCommitLife(change)) return {DomainStatus::Stale,{}};
    // Do108 removes the actual GUID before writing the eater's HP. It does
    // not leave a hidden corpse which another AI can resolve later.
    ports_.effects.forgetUnit(target);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,target});
    ports_.areas.removeRoomUnit(target);state_.actors.erase(target);
    if(!commitLife(std::move(change))) throw std::logic_error("Regurgitator resource commit lost its reservation");
    return {DomainStatus::Applied,std::monostate{}};
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
    if(caster->rule.ai.kind==MonsterAiKind::FetishShaman) {
        const int capability=caster->rule.ai.params[1];
        const auto &classes=caster->rule.resurrectionClasses;
        if(classes.empty() || dead->alignment()!=UnitAlignment::Evil ||
            (dead->rule.baseClass!=classes[0] && (classes.size()<2 || dead->rule.baseClass!=classes[1] || capability==1)) ||
            (!capability && minionOwner(corpse)!=source) ||
            (capability<3 && nativeUniqueMonster(dead->identity.rank))) return false;
        for(const auto state:ports_.effects.unitStates(corpse,tick))
            if(dead->rule.states->at(state).corpseUnselectable) return false;
        const Vec delta{std::floor(caster->position.x)-std::floor(dead->position.x),
            std::floor(caster->position.y)-std::floor(dead->position.y)};
        const int radius=caster->rule.ai.params[4];
        return delta.x*delta.x+delta.y*delta.y<=radius*radius &&
            ports_.areas.at(caster->area).definition.activation.nearby(caster->position,dead->position);
    }
    const int radius=caster->rule.ai.params[3];
    const bool unique=nativeUniqueMonster(caster->identity.rank) && caster->identity.rank!=MonsterRank::Champion;
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
bool System::resurrectionPlacement(EntityId corpse) const {
    const auto *actor=find(corpse);if(!actor) return false;
    const auto &grid=ports_.areas.at(actor->area).definition.collision;
    if(!grid.walkable(actor->position,actor->rule.collision)) return false;
    for(const auto &[id,other]:state_.actors) if(id!=corpse && other.area==actor->area && other.life>0 && (actor->position-other.position).length()<1.f) return false;
    for(const auto &[id,p]:ports_.players.all()) { (void)id;if(p.entered && p.area==actor->area && p.persistent.player.hp>0 && (actor->position-p.position).length()<1.f) return false; }
    return true;
}
DomainResult<> System::resurrect(EntityId source,EntityId corpse,uint64_t tick) {
    if(!resurrectionTarget(source,corpse,tick) || !resurrectionPlacement(corpse)) return {DomainStatus::Unavailable,{}};
    auto &actor=state_.actors.at(corpse);
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{HitFact{corpse,1,actor.area,128,false,actor.position}}})) return {DomainStatus::Capacity,{}};
    actor.life=actor.maximumLife;actor.chilledUntil=actor.frozenUntil=0;actor.shatterActive=false;actor.corpse={};actor.poison.reset();
    refreshCollision(actor);
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
    auto it=state_.actors.find(id);if(it==state_.actors.end() || !it->second.alive() || !it->second.rule.nestChild) return {DomainStatus::InvalidActor,{}};
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
DomainResult<> System::setMinionSpawnClass(EntityId id,int nativeClass) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    unit.minionSpawnClass=nativeClass;unit.minionSpawnClassAiRevision=unit.aiFunctionRevision;++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::canSpawnNestChild(EntityId id,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) const {
    const auto *nest=find(id);if(!nest || !nest->alive() || (!nest->rule.nestChild && nest->rule.ai.kind!=MonsterAiKind::GenericSpawner && nest->rule.ai.kind!=MonsterAiKind::Nihlathak)) return false;
    if(nest->rule.ai.kind==MonsterAiKind::GenericSpawner || nest->rule.ai.kind==MonsterAiKind::Nihlathak) {
        if(!collision) return false;
        const auto &area=ports_.areas.at(nest->area).definition;
        // Fn129 probes EvilHut's native preflight (+2,+4), independently
        // of MonStats.spawnx/spawny (+2,+5 in the current MPQ).
        const auto point=nest->position+area.origin+Vec{2,4};
        const int x=int(std::floor(point.x)),y=int(std::floor(point.y));
        const auto room=ports_.areas.roomAtPosition(nest->area,nest->position,x,y);
        return room && !ports_.areas.nativeCollisionMask(*room,x,y,2,0x3c01,collision);
    }
    if(nest->rule.ai.kind==MonsterAiKind::FoulCrowNest) {
        if(!collision) return false;
        const auto &area=ports_.areas.at(nest->area).definition;
        const auto point=nest->position+area.origin+Vec{0,3};
        const int x=int(std::floor(point.x)),y=int(std::floor(point.y));
        const auto room=ports_.areas.roomAtPosition(nest->area,nest->position,x,y);
        return room && !ports_.areas.nativeCollisionMask(*room,x,y,2,0x3c01,collision);
    }
    auto random=nest->combatRandom;
    return bool(nestBirthPoint(id,nest->position+nest->rule.spawnOffset,random));
}
DomainResult<> System::reserveNestSpawn(EntityId id,Vec position,bool active,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(unit.revision>UINT64_MAX-2) return {DomainStatus::Capacity,{}};
    const auto changed=toggleAiState(id,unit.rule.uninterruptableState,active,tick);if(!changed) return changed;
    if(active) unit.spawnReservation=position;else unit.spawnReservation.reset();
    refreshCollision(unit);
    ++unit.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<EntityId> System::spawnNativeNestChild(EntityId id,Vec position,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision,std::optional<int> fallbackClass,int attempts,bool storedOwner) {
    const auto *source=find(id);if(!source) return {DomainStatus::InvalidActor,{}};
    if(source->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto &area=ports_.areas.at(source->area).definition;
    const auto *prototype=source->rule.nestChild.get();
    if(!prototype && fallbackClass) if(const auto chosen=area.nativeMonsterClasses.find(*fallbackClass);chosen!=area.nativeMonsterClasses.end()) prototype=&chosen->second;
    if(!prototype) return {DomainStatus::Unavailable,{}};
    const auto &child=*prototype;
    const auto global=position+area.origin;
    const auto room=ports_.areas.roomAtPosition(source->area,source->position,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Unavailable,{}};
    const auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    auto identity=child.identity;identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;identity.pd2Binding=-1;
    identity.spawnKey=source->identity.spawnKey+".native-child."+std::to_string(ports_.ids.cursor());
    auto rule=child.rule;rule.noTreasure=rule.noExperience=true;
    Admission request{identity,child.implementation,room->area,global-ports_.areas.at(room->area).definition.origin,true,std::move(rule),{},tick};
    request.birthTicks=source->rule.spawnTicks;
    auto birth=prepareRoomSpawn(request,*loan.value,attempts,{},false,collision);
    if(!birth) return {birth.status,{}};
    if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
    const auto born=(*birth.value)->actor();(*birth.value)->publish();
    if(born) {
        auto &unit=state_.actors.at(born);unit.rewardComplete=true;
        if(storedOwner) setStoredOwner(unit,id);
        auto &parent=state_.actors.at(id);++parent.nestSpawned;++parent.revision;
    }
    return {DomainStatus::Applied,born};
}
DomainResult<EntityId> System::spawnEvilHoleChild(EntityId id,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *source=find(id);
    if(!source || !source->alive() || source->rule.ai.kind!=MonsterAiKind::EvilHole) return {DomainStatus::InvalidActor,{}};
    const auto *prototype=source->rule.nestChild.get();if(!prototype) return {DomainStatus::Unavailable,{}};
    const auto &area=ports_.areas.at(source->area).definition;
    const auto global=source->position+area.origin;
    const auto room=ports_.areas.roomAtPosition(source->area,source->position,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Applied,EntityId{}};
    const auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    auto identity=prototype->identity;identity.origin=SpawnOrigin::Summoned;identity.ownerSpawnKey.clear();
    identity.spawnKey=source->identity.spawnKey+".hole-child."+std::to_string(ports_.ids.cursor());
    Admission request{identity,prototype->implementation,room->area,global-ports_.areas.at(room->area).definition.origin,true,prototype->rule,{},tick};
    request.birthTicks=source->rule.spawnTicks;
    // Fn076 uses radius 2 and flags 0x42: no normal modifiers or party.
    auto birth=prepareRoomSpawn(request,*loan.value,2,{},false,collision);
    if(!birth) return {birth.status,{}};
    const auto born=(*birth.value)->actor();
    std::vector<DomainFact> facts;
    if(born) {
        auto &child=(*birth.value)->data_->actors.actors.at(born);
        child.rule.noExperience=child.rule.noTreasure=true;child.rewardComplete=true;
        child.admissionRule=std::make_shared<const MonsterRule>(child.rule);
        if(source->rule.uberMinionState>=0) {
            child.aiStates.insert(source->rule.uberMinionState);
            facts.emplace_back(StateFact{born,1,child.area,source->rule.uberMinionState,true});
            facts.emplace_back(OverlayFact{born,1,child.area,202});
        }
    }
    if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
    if(!facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},room->area},std::move(facts)});
        if(!sent) return {sent.status,{}};
    }
    (*birth.value)->publish();return {DomainStatus::Applied,born};
}
DomainResult<std::optional<Vec>> System::findBaalTeleportPosition(EntityId id,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *unit=find(id);if(!unit || !unit->alive() || !collision) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(unit->area).definition;
    const auto *bounds=area.activation.room(unit->position);if(!bounds) return {DomainStatus::Unavailable,{}};
    const auto prototype=area.nativeMonsterClasses.find(unit->rule.nativeClass);
    if(prototype==area.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const RoomView room{unit->area,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    auto loan=ports_.areas.roomRandom(room);if(!loan) return {loan.status,{}};
    std::optional<Vec> position;
    for(int attempt=0;attempt<20;++attempt) {
        // MonsterRegion::sub_6FC66260 replaces its input coordinates with
        // room-seed draws. With a7=0 it skips warp exclusion, not probing.
        const Vec point{float(bounds->x+1+int(limitedRandom(loan.value->next,uint32_t(bounds->width-1)))),
            float(bounds->y+1+int(limitedRandom(loan.value->next,uint32_t(bounds->height-1))))};
        auto identity=prototype->second.identity;identity.spawnKey="baal-teleport-probe."+std::to_string(id.value);
        Admission request{identity,prototype->second.implementation,unit->area,point,true,prototype->second.rule,{},tick};
        const auto probe=searchRoomSpawn(request,*loan.value,-1,collision);if(!probe) return {probe.status,{}};
        if(*probe.value) {position=**probe.value;break;}
    }
    if(!ports_.areas.validateRoomRandom(*loan.value)) return {DomainStatus::Stale,{}};
    ports_.areas.publishRoomRandom(*loan.value);
    return {DomainStatus::Applied,std::optional<std::optional<Vec>>{std::in_place,position}};
}
DomainResult<> System::setBaalThroneWave(EntityId id,int expected,int next) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(unit.rule.ai.kind!=MonsterAiKind::BaalThrone || next<0 || next>5) return {DomainStatus::InvalidRequest,{}};
    if(unit.baalThroneWave==next) return {DomainStatus::Applied,std::monostate{}};
    if(unit.baalThroneWave!=expected) return {DomainStatus::Stale,{}};
    if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    unit.baalThroneWave=next;++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::assignBaalNova(EntityId id,uint64_t tick) {
    return assignBaalSkill(id,"baal nova",{},tick);
}
DomainResult<> System::assignBaalMonsterSpawn(EntityId id,int mappedPreset,uint64_t tick) {
    if(mappedPreset<0) return {DomainStatus::InvalidRequest,{}};
    return assignBaalSkill(id,"baal monster spawn",mappedPreset,tick);
}
DomainResult<> System::assignBaalSkill(EntityId id,std::string_view name,std::optional<int> mappedPreset,uint64_t tick) {
    const auto *live=find(id);if(!live || !live->alive() || !live->rule.formulas) return {DomainStatus::InvalidActor,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto found=live->rule.formulas->skillIds.find(std::string(name));
    if(found==live->rule.formulas->skillIds.end() || !live->rule.skillActions.contains(found->second)) return {DomainStatus::Unavailable,{}};
    const auto &program=live->rule.formulas->skills.at(found->second);
    if(program.passiveState>0) return {DomainStatus::NotImplemented,{}};
    auto candidate=*live;const auto *states=ports_.effects.unitEffects(id);
    // MainSkillHandler / BaalThrone assign rank 1. The original difficulty
    // bonus is not reapplied by SKILLS_AssignSkill.
    attributes::assignMonsterSkill(candidate,states?*states:CombatEffectSet{},found->second,1,tick,{});
    if(mappedPreset) candidate.baalSpawnPreset=*mappedPreset;
    ++candidate.revision;
    auto plan=ports_.effects.prepareDamageStates(std::move(candidate),{},tick);
    if(!ports_.effects.validateAuraUnits(plan)) return {DomainStatus::Conflict,{}};
    if(!plan.facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},live->area},std::move(plan.facts)});
        if(!sent) return {sent.status,{}};
    }
    ports_.effects.publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<EntityId> System::spawnMinionChild(EntityId id,Vec position,int skill,MonsterSpawnParameters parameters,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision,MinionBirthProcedure procedure) {
    const auto *source=find(id);if(!source) return {DomainStatus::InvalidActor,{}};
    const auto &originArea=ports_.areas.at(source->area).definition;
    const bool nativeBaal=procedure!=MinionBirthProcedure::Spawner;
    const bool clone=procedure==MinionBirthProcedure::BaalClone;
    const PreparedMonster *prototype=source->rule.nestChild && source->rule.nestChild->rule.nativeClass==parameters.nativeClass?source->rule.nestChild.get():nullptr;
    if(!prototype) if(const auto found=originArea.nativeMonsterClasses.find(parameters.nativeClass);found!=originArea.nativeMonsterClasses.end()) prototype=&found->second;
    if(!prototype) return {nativeBaal?DomainStatus::NotImplemented:DomainStatus::Unavailable,{}};
    if(parameters.nativeMode!=1 && parameters.nativeMode!=8 && !(nativeBaal && parameters.nativeMode==4)) return {DomainStatus::NotImplemented,{}};
    const auto global=position+originArea.origin;
    const auto room=ports_.areas.roomAtPosition(source->area,source->position,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Unavailable,{}};
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    auto identity=prototype->identity;identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;
    identity.spawnKey=source->identity.spawnKey+".minion-child."+std::to_string(ports_.ids.cursor());
    Admission request{identity,prototype->implementation,room->area,global-ports_.areas.at(room->area).definition.origin,true,prototype->rule,{},tick};
    request.birthTicks=parameters.birthTicks;
    if(clone) request.minionOwner=id;
    auto birth=prepareRoomSpawn(request,*loan.value,-1,{},false,collision);
    if(!birth) return {birth.status,{}};
    if(!(*birth.value)->actor() && !nativeBaal) {
        // The failed origin probe consumed the room's orientation/sign
        // draws. Continue the same loan through the four-ring fallback.
        *loan.value=*(*birth.value)->data_->room;
        birth=prepareRoomSpawn(request,*loan.value,4,{},false,collision);
        if(!birth) return {birth.status,{}};
    }
    const auto born=(*birth.value)->actor();
    if(born) {
        auto &child=(*birth.value)->data_->actors.actors.at(born);
        child.rule.noExperience=child.rule.noTreasure=true;child.rewardComplete=true;
        child.admissionRule=std::make_shared<const MonsterRule>(child.rule);
        setStoredOwner(child,id);
        if(nativeBaal) {
            // GetMinionSpawnInfo chooses A1, not the ordinary S1 rise mode.
            // The initial mode has no target/contact. Do140 then replaces
            // the initial AI wake-up with Idle(15).
            recordNativeMode(child,parameters.nativeMode);
            if(parameters.nativeMode==4) {
                const auto attack=child.rule.attacks.find(parameters.nativeMode);
                if(attack==child.rule.attacks.end()) return {DomainStatus::Unavailable,{}};
                child.riseMode=parameters.nativeMode;
                child.riseUntil=child.busyUntil=tick+uint64_t(attack->second.duration);
                child.neutralAt=child.riseUntil;
            }
            child.aiThinkAt=tick+15;++child.aiThinkRevision;
            child.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,born},FrameEventType::AiThink,child.aiThinkAt}));
        }
        if(clone) {
            const auto values=ports_.effects.unitStatValues(id,tick);if(!values) return {DomainStatus::Unavailable,{}};
            const auto maximum=nativeStatValue(*values,*source->rule.stats,"maxhp")/3;
            const auto life=nativeStatValue(*values,*source->rule.stats,"hitpoints")/3;
            attributes::MonsterResourceTrace resources(child,tick);
            resources.writeBaseStat("maxhp",maximum,{});
            resources.writeBaseStat("hpregen",0,{});
            resources.seedLife(Fixed8::fromRaw(life));
            auto change=resources.take();child.nativeStats=std::move(change.intrinsic);
            child.maximumLife=change.maximum.raw();child.life=change.life.raw();child.modeAlive=child.life==0;
            child.ownsNativeMinions=true;child.transferMinionsOnDeath=false;
            auto &parent=(*birth.value)->data_->actors.actors.at(id);
            parent.nativeMinionOwner=id;parent.ownsNativeMinions=true;parent.transferMinionsOnDeath=false;
            setStoredOwner(parent,born);setStoredOwner(child,id);
        }
    }
    if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
    const int overlay=nativeBaal?-1:source->rule.formulas->skills.at(skill).summonOverlay;
    if(born && clone) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},source->area},{SoundFact{id,1,source->area,16}}});
        if(!sent) return {sent.status,{}};
    }
    if(born && overlay>=0) {
        const auto &child=(*birth.value)->data_->actors.actors.at(born);
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},child.area},{OverlayFact{born,1,child.area,overlay}}});
        if(!sent) return {sent.status,{}};
    }
    (*birth.value)->publish();
    return born?DomainResult<EntityId>{DomainStatus::Applied,born}:DomainResult<EntityId>{DomainStatus::Unavailable,{}};
}
std::optional<Vec> System::nestBirthPoint(EntityId id,Vec origin,uint64_t &random) const {
    const auto *source=find(id);if(!source || !source->alive() || !source->rule.nestChild) return {};
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
    if(*length>0) spec.duration=uint64_t(*length);
    spec.refreshPassivesOnRemoval=true;
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
    resources.seedLife(Fixed8::fromRaw(nativeChecked(std::min(std::max<int64_t>(patient->life+amount,1),patient->maximumLife))));
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
    const auto *source=find(id);if(!source) return {DomainStatus::InvalidActor,{}};
    return teleport(id,PointTarget{source->area,ports_.areas.at(source->area).generation,point},tick,heal,nativeFreePosition);
}
DomainResult<> System::teleport(EntityId id,PointTarget target,uint64_t tick,int heal,bool nativeFreePosition) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &m=it->second;const auto *destination=ports_.areas.find(target.area);
    const auto &source=ports_.areas.at(m.area);
    if(!destination || destination->generation!=target.generation || destination->provisionalPd2Map ||
       source.definition.act!=destination->definition.act || !std::isfinite(target.position.x) || !std::isfinite(target.position.y))
        return {DomainStatus::Stale,{}};
    if(target.area!=m.area && !nativeFreePosition) return {DomainStatus::InvalidRequest,{}};
    if(target.area!=m.area) {
        const auto global=target.position+destination->definition.origin;
        const auto room=ports_.areas.roomAtPosition(m.area,m.position,int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)));
        if(!room || room->area!=target.area) return {DomainStatus::Unavailable,{}};
    }
    if(m.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto grid=ports_.spatial.navigationGrid(target.area,id);const auto point=target.position;
    const Vec center{.5f,.5f};
    if(!grid.walkable(point+center,{uint16_t(nativeFreePosition?0x1c09:0x3c01),m.rule.size}) ||
       (!nativeFreePosition && !grid.segment(m.position+center,point+center,{}, {0x0c01,1}))) return {DomainStatus::Unavailable,{}};
    // Native monster relocation is projected by 0x6D snapshot correction.
    (void)tick;stop(id);m.area=target.area;m.position=point;ports_.areas.moveRoomUnit(id,m.area,m.position);refreshCollision(m);m.life=std::min(m.maximumLife,m.life+int64_t(std::max(0,heal))*256);++m.revision;
    return {DomainStatus::Applied,std::monostate{}};
}

}
