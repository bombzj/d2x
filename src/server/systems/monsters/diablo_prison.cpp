#include "system.hpp"
#include "server/area_store.hpp"
#include <array>
#include <cmath>
#include <string>

namespace d2x::server::monsters {
DomainResult<> System::spawnDiabloPrison(RegionId region,Vec target,int nativeClass,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto result=diabloPrison(region,target,nativeClass,tick,false,collision);
    return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<bool> System::probeDiabloPrison(RegionId region,Vec target,int nativeClass,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    return diabloPrison(region,target,nativeClass,tick,true,collision);
}
DomainResult<bool> System::diabloPrison(RegionId region,Vec target,int nativeClass,uint64_t tick,bool probe,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *area=ports_.areas.find(region);
    if(!area || !collision || !std::isfinite(target.x) || !std::isfinite(target.y)) return {DomainStatus::Unavailable,{}};
    if(!probe && area->definition.town) return {DomainStatus::Unavailable,{}};
    for(int index=0;index<4;++index) {
        const auto part=area->definition.nativeMonsterClasses.find(nativeClass+index);
        if(part==area->definition.nativeMonsterClasses.end() || part->second.implementation!=MonsterKind::BonePrison)
            return {DomainStatus::NotImplemented,{}};
    }
    // Retail helper RVA EFB80 / table10F3C0: four consecutive classes,
    // mode S1, fixed offsets, root type1 followed by three type3 members.
    constexpr std::array<Vec,4> offsets{{{1,1},{1,-1},{-1,-1},{-1,1}}};
    const auto center=target+area->definition.origin;
    // The group passes the target's room to SpawnNormalMonster. Its exact
    // point search checks that room's rectangle before resolving adjacency;
    // selecting a fresh room for each offset admits border positions retail
    // rejects and advances the wrong room seed.
    const auto room=ports_.areas.roomAtPosition(region,target,int(std::floor(center.x)),int(std::floor(center.y)));
    if(!room) return {DomainStatus::Applied,false};
    const auto &destination=ports_.areas.at(room->area);
    const auto borrowed=ports_.areas.roomRandom(*room);if(!borrowed) return {borrowed.status,{}};
    auto loan=*borrowed.value;
    auto ids=ports_.ids;auto random=ports_.random;
    NativeBirthBatch batch;EntityId root;bool complete=true;
    const auto sample=[&](RoomView room,int32_t x,int32_t y) {
        auto mask=collision(room,x,y);
        for(const auto &[id,part]:batch.actors) {
            (void)id;if(part.area!=room.area) continue;
            const auto position=part.position+ports_.areas.at(part.area).definition.origin;
            mask|=nativeCollisionFootprint(part.rule.collisionPattern,x-int(std::floor(position.x)),y-int(std::floor(position.y)),0x100);
        }
        return mask;
    };
    for(size_t index=0;index<offsets.size();++index) {
        if(!probe && index && !root) break;
        const auto point=center+offsets[index];
        const auto &prototype=area->definition.nativeMonsterClasses.at(nativeClass+int(index));
        Admission request;request.identity=prototype.identity;request.identity.origin=SpawnOrigin::Summoned;
        request.identity.spawnKey="diablo-prison."+std::to_string(ids.cursor());request.identity.ownerSpawnKey.clear();
        request.implementation=prototype.implementation;request.area=room->area;
        request.position=point-destination.definition.origin;request.hostile=true;request.rule=prototype.rule;
        request.tick=tick;request.birthTicks=prototype.rule.spawnTicks;
        if(probe) {
            // Flags1 forces spawn type4 for every record. A successful probe
            // returns sentinel1 before allocation; it leaves no collision
            // footprint, but still publishes the room's search draws. Stop
            // at the first failed record, including its consumed draws.
            const auto searched=searchRoomSpawn(request,loan,-1,collision);
            if(!searched) return {searched.status,{}};
            if(!*searched.value) {complete=false;break;}
            continue;
        }
        // SpawnNormalMonster with flags0 searches only the supplied point.
        const auto spawned=stageRoomSpawn(request,loan,-1,ids,random,batch,sample);
        if(!spawned) return {spawned.status,{}};
        if(!*spawned.value) continue;
        auto &part=batch.actors.at(*spawned.value);
        if(!index) {root=part.id;part.nativeMinionOwner=root;part.ownsNativeMinions=true;}
        else {part.nativeMinionOwner=root;batch.actors.at(root).nativeMinions.insert(batch.actors.at(root).nativeMinions.begin(),part.id);}
    }
    if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};
    if(probe) {ports_.areas.publishRoomRandom(loan);return {DomainStatus::Applied,complete};}
    const auto allocated=ids.cursor()-ports_.ids.cursor();
    auto publication=prepareActorPublication(std::move(batch.actors),allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    ports_.areas.publishRoomRandom(loan);
    publishBirthComponents(batch);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,bool(root)};
}
}
