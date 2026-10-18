#include "system.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::monsters {
DomainResult<> System::spawnHydra(EntityId id,const HydraRules &rules,const SkillCastSpec &skill,Vec target,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *source=find(id);
    if(!source || !source->alive() || !collision || !skill.summon || !skill.summon->native || skill.hydraFrames<=0)
        return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(source->area);
    const auto globalTarget=target+area.definition.origin;
    const auto targetRoom=ports_.areas.roomAtPosition(source->area,source->position,int(std::floor(globalTarget.x)),int(std::floor(globalTarget.y)));
    if(!targetRoom || ports_.areas.at(targetRoom->area).definition.town) return {DomainStatus::Unavailable,{}};
    constexpr std::array<Vec,3> offsets{{{-1,-1},{0,0},{1,-1}}};
    std::array<std::optional<Vec>,3> positions;
    std::array<RegionId,3> regions;
    std::map<RoomView,RoomRandomLoan> loans;
    const auto sample=[&](RoomView room,int32_t x,int32_t y) {
        auto mask=collision(room,x,y);
        for(size_t index=0;index<positions.size();++index) if(positions[index]) {
            const auto point=*positions[index]+ports_.areas.at(regions[index]).definition.origin;
            mask|=nativeCollisionFootprint(rules.heads[index].rules.at(size_t(source->rule.difficulty)).collisionPattern,
                x-int(std::floor(point.x)),y-int(std::floor(point.y)),0x100);
        }
        return mask;
    };
    for(size_t index=0;index<positions.size();++index) {
        const auto global=globalTarget+offsets[index];
        const auto room=ports_.areas.roomAtPosition(targetRoom->area,globalTarget-ports_.areas.at(targetRoom->area).definition.origin,
            int(std::floor(global.x)),int(std::floor(global.y)));
        if(!room || ports_.areas.at(room->area).definition.town) continue;
        auto loan=loans.find(*room);
        if(loan==loans.end()) {
            const auto borrowed=ports_.areas.roomRandom(*room);if(!borrowed) return {borrowed.status,{}};
            loan=loans.emplace(*room,*borrowed.value).first;
        }
        regions[index]=room->area;
        Admission request;request.identity.monster=rules.heads[index].definition.code;request.identity.spawnKey="hydra.search";
        request.area=room->area;request.position=global-ports_.areas.at(room->area).definition.origin;
        request.rule=rules.heads[index].rules.at(size_t(source->rule.difficulty));request.tick=tick;
        auto point=searchRoomSpawn(request,loan->second,-1,sample);if(!point) return {point.status,{}};
        if(!*point.value) point=searchRoomSpawn(request,loan->second,4,sample);
        if(!point) return {point.status,{}};
        positions[index]=*point.value;
    }
    for(const auto &[room,loan]:loans) {(void)room;if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};}
    if(std::none_of(positions.begin(),positions.end(),[](const auto &point){return point.has_value();})) {
        for(const auto &[room,loan]:loans) {(void)room;ports_.areas.publishRoomRandom(loan);}
        return {DomainStatus::Unavailable,{}};
    }
    auto random=ports_.random;
    const ActorContext caster{{},id,source->area,area.generation,0,tick};
    auto actors=prepareHydra(caster,rules,*skill.summon,positions,random,regions);if(!actors) return {actors.status,{}};
    const auto allocated=actors.value->size();
    for(auto &[head,unit]:*actors.value) {
        (void)head;
        unit.hydraExpires=tick+uint64_t(skill.hydraFrames);
        unit.identity.ownerSpawnKey=source->identity.spawnKey;
    }
    // SetOwnerData stores OwnerGUIDEx only. It does not append the heads to
    // the council's minion list. PlayerPets returns immediately for monsters.
    auto publication=prepareActorPublication(std::move(*actors.value),allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    for(const auto &[room,loan]:loans) {(void)room;ports_.areas.publishRoomRandom(loan);}
    publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
}
