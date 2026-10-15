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
        (!d.actor || (system.ports_.random==d.randomExpected && system.validateActors(d.actors)));
}
void NativeBirthPublication::publish() noexcept {
    if(!validate()) std::terminate();
    auto &d=*data_;auto &system=*d.system;
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
        return !ports_.areas.nativeCollisionMask(room,column,row,rule.spawnCollision.size,rule.spawnCollision.mask,collision);
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
    auto actor=prepareAdmissionActor(admission,data->actor,data->randomNext,data->palettes);
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
    data->actors=prepareActorPublication(std::move(actors),1);
    return finish();
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
    actor.corpseUnavailable|=corpseUnavailable;++actor.revision;
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
    if(!unit || !deadMode(id,tick) || !unit->rule.corpseSelectable || unit->corpseUnavailable || unit->corpseSearched) return false;
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
    if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.corpseUnavailable=true;++it->second.revision;}
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
        dead->corpseUnavailable || dead->corpseSearched || tick<dead->busyUntil || !dead->rewardComplete || dead->area!=caster->area) return false;
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
    actor.life=actor.maximumLife;actor.chilledUntil=actor.frozenUntil=0;actor.shatterActive=false;actor.deathStates.clear();actor.poison.reset();
    actor.riseUntil=actor.busyUntil=tick+uint64_t(actor.rule.resurrectionTicks);actor.riseMode=actor.rule.resurrectionMode;
    actor.neutralAt=actor.riseUntil;
    actor.killer={};actor.route.clear();actor.moving=actor.running=false;actor.movementTarget={};
    actor.hitOccurrence=0;actor.knockedUntil=0;actor.knockbackGoal.reset();++actor.revision;
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
DomainResult<> System::activateWeb(EntityId id,uint64_t tick) {
    const auto *live=find(id);if(!live || !live->alive() || !live->rule.web) return {DomainStatus::InvalidActor,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto actor=*live;const auto &web=*actor.rule.web;
    actor.webUntil=tick+uint64_t(web.auraFrames);actor.webOrigin=actor.position;actor.webRandom=childRandom(actor.combatRandom);++actor.revision;
    effects::UnitDamageStates update;CombatEffectSpec spec;spec.state=web.aura;
    spec.source={CombatEffectSource::Monster,id,actor.rule.web->missile,0};spec.stacking=EffectStacking::ReuseState;
    spec.duration=uint64_t(web.auraFrames);spec.refreshPassivesOnRemoval=true;update.status=std::move(spec);
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
