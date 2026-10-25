#include "../system.hpp"
#include "../native_birth.hpp"
#include "../native_birth_data.hpp"
#include "server/area_store.hpp"
#include <cmath>

namespace d2x::server::monsters {
DomainResult<std::shared_ptr<NativeBirthPublication>> System::preparePd2MissileSpawn(RegionId region,Vec observer,Vec position,
    int definition,int mode,uint32_t flags,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    // Core 3A5360 -> 39F0A0 -> retail Game47CF0 (preset regular monster).
    // Other unit-flag combinations and modes need their actual lifecycle.
    constexpr uint32_t noTreasure=0x20000,noExperience=0x4000000;
    if((mode!=1 && mode!=8) || (flags&~(noTreasure|noExperience))) return {DomainStatus::NotImplemented,{}};
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
    auto first=prepareRoomSpawn(request,*loan.value,-1,{},false,collision);
    if(!first || (*first.value)->actor() || !capability->second.retry) return first;
    return prepareRoomSpawn(request,*(*first.value)->data_->room,4,{},false,collision);
}
}
