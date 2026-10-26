#include "../system.hpp"
#include "../native_birth.hpp"
#include "../native_birth_data.hpp"
#include "server/area_store.hpp"
#include "server/systems/attributes/pd2/map_values.hpp"
#include "server/systems/effects/system.hpp"
#include <cmath>

namespace d2x::server::monsters {
DomainResult<std::shared_ptr<NativeBirthPublication>> System::preparePd2MissileSpawn(RegionId region,Vec observer,Vec position,
    int definition,int mode,uint32_t flags,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    return preparePd2NativeSpawn(region,observer,position,definition,mode,flags,tick,collision,true);
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::preparePd2NativeSpawn(RegionId region,Vec observer,Vec position,
    int definition,int mode,uint32_t flags,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision,bool preset) {
    // Core 3A5360 -> 39F0A0 -> retail Game47CF0 (preset regular monster).
    // INITSEEDSET is already supplied by native room admission; setting it
    // after birth must not reseed the child. Current missile 979 additionally
    // ORs all three target bits. Partial target masks need independent flags.
    constexpr uint32_t targetFlags=0x0e,seedInitialized=0x10,noTreasure=0x20000,noExperience=0x4000000;
    if((mode!=1 && mode!=8) || (flags&~(targetFlags|seedInitialized|noTreasure|noExperience)) ||
       ((flags&targetFlags) && (flags&targetFlags)!=targetFlags)) return {DomainStatus::NotImplemented,{}};
    const auto *area=ports_.areas.find(region);
    if(!area || !collision || !std::isfinite(position.x) || !std::isfinite(position.y) ||
       !std::isfinite(observer.x) || !std::isfinite(observer.y)) return {DomainStatus::InvalidRequest,{}};
    const auto global=position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(region,observer,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Unavailable,{}};
    area=ports_.areas.find(room->area);
    const auto capability=area->definition.pd2MissileSpawns.find(definition);
    const auto prototype=area->definition.nativeMonsterClasses.find(definition);
    if(capability==area->definition.pd2MissileSpawns.end() || prototype==area->definition.nativeMonsterClasses.end())
        return {DomainStatus::NotImplemented,{}};
    if(mode==8 && capability->second.skillOneTicks<=0) return {DomainStatus::NotImplemented,{}};
    auto identity=prototype->second.identity;
    if(prototype->second.rule.boss) identity.rank=MonsterRank::Boss;
    identity.spawnKey="pd2-missile."+std::to_string(ports_.ids.cursor());
    identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;
    auto rule=prototype->second.rule;
    rule.noTreasure|=bool(flags&noTreasure);rule.noExperience|=bool(flags&noExperience);
    const bool hostile=rule.alignment==0;
    Admission request{identity,prototype->second.implementation,room->area,global-area->definition.origin,
        hostile,std::move(rule),{},tick};
    if(mode==8) request.birthTicks=capability->second.skillOneTicks;
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    // Even the radius-zero -1 attempt consumes orientation/sign draws.
    // Retain its publication or continue from its loan, never probe twice.
    auto prepared=prepareRoomSpawn(request,*loan.value,-1,{},false,collision);
    if(prepared && !(*prepared.value)->actor() && preset && capability->second.retry)
        prepared=prepareRoomSpawn(request,*(*prepared.value)->data_->room,4,{},false,collision);
    if(prepared && (*prepared.value)->actor() && (flags&targetFlags)==targetFlags)
        (*prepared.value)->data_->actors.actors.at((*prepared.value)->actor()).targetFlagsDisabled=false;
    return prepared;
}
DomainResult<EntityId> System::admitPd2NativeChild(EntityId owner,RegionId region,Vec position,int definition,int mode,uint32_t flags,
    bool storedOwner,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *source=find(owner);
    if(storedOwner && !source) return {DomainStatus::InvalidActor,{}};
    auto born=preparePd2NativeSpawn(region,source && source->area==region?source->position:position,position,definition,mode,flags,tick,collision,false);
    if(!born) return {born.status,{}};
    const auto id=(*born.value)->actor();
    if(id && storedOwner) setStoredOwner((*born.value)->data_->actors.actors.at(id),owner);
    if(!(*born.value)->validate()) return {DomainStatus::Stale,{}};
    (*born.value)->publish();return {DomainStatus::Applied,id};
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::preparePd2WandererSpawn(RegionId region,Vec observer,Vec position,
    int definition,bool actBoss,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    auto prepared=preparePd2MissileSpawn(region,observer,position,definition,1,0,tick,collision);
    if(!prepared || !(*prepared.value)->actor()) return prepared;
    auto &publication=(*prepared.value)->data_->actors;
    attributes::preparePd2EventMonster(publication.actors.at((*prepared.value)->actor()),actBoss?50:100,tick,
        actBoss?(definition==544?1:2):4);
    auto states=ports_.effects.prepareBirthEffects(publication.actors);
    publication.birthEffects=std::make_shared<effects::BirthEffectsPublication>(std::move(states));
    return prepared;
}
DomainResult<> System::applyPd2WandererState(EntityId id,bool actBoss,uint64_t tick) {
    const auto *unit=find(id);if(!unit) return {DomainStatus::InvalidActor,{}};
    auto candidate=*unit;
    if(const auto *effects=ports_.effects.unitEffects(id)) candidate.initialEffects=std::make_shared<CombatEffectSet>(*effects);
    attributes::preparePd2EventMonster(candidate,actBoss?50:100,tick,actBoss?(candidate.rule.nativeClass==544?1:2):4);
    ++candidate.revision;
    auto publication=prepareActorPublication({{id,std::move(candidate)}},0);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishActors(publication);return {DomainStatus::Applied,std::monostate{}};
}
}
