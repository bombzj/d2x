#include "system.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
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
    if(!int(globalTarget.x) || !int(globalTarget.y) || !targetRoom || ports_.areas.at(targetRoom->area).definition.town)
        return {DomainStatus::Unavailable,{}};
    auto random=ports_.random;
    const ActorContext caster{{},id,source->area,area.generation,0,tick};
    auto prepared=prepareHydra(caster,rules,skill,globalTarget,random,collision);if(!prepared) return {prepared.status,{}};
    auto &birth=*prepared.value;
    const auto allocated=birth.actors.size();
    for(auto &[head,unit]:birth.actors) {
        (void)head;
        unit.identity.ownerSpawnKey=source->identity.spawnKey;
    }
    // SetOwnerData stores OwnerGUIDEx only. It does not append the heads to
    // the council's minion list. PlayerPets returns immediately for monsters.
    for(const auto &loan:birth.roomRandomLoans) if(!ports_.areas.validateRoomRandom(*loan)) return {DomainStatus::Stale,{}};
    auto publication=prepareActorPublication(std::move(birth.actors),allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    for(const auto &loan:birth.roomRandomLoans) ports_.areas.publishRoomRandom(*loan);
    if(!allocated) return {DomainStatus::Unavailable,{}};
    publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
}
