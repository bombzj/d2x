#include "system.hpp"
#include "native_birth.hpp"
#include "native_birth_data.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <cmath>

namespace d2x::server::monsters {
DomainResult<EntityId> System::spawnNativePet(EntityId id,Vec position,MonsterSpawnParameters parameters,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *owner=find(id);if(!owner || !owner->alive()) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(owner->area).definition;
    const auto prototype=area.nativeMonsterClasses.find(parameters.nativeClass);
    if(prototype==area.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const auto global=position+area.origin;
    const auto room=ports_.areas.roomAtPosition(owner->area,owner->position,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Unavailable,{}};
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    auto identity=prototype->second.identity;identity.origin=SpawnOrigin::Summoned;
    identity.ownerSpawnKey.clear();identity.spawnParentKey.clear();identity.pd2Binding=-1;
    identity.spawnKey=owner->identity.spawnKey+".native-pet."+std::to_string(ports_.ids.cursor());
    Admission request{identity,prototype->second.implementation,room->area,global-ports_.areas.at(room->area).definition.origin,
        true,prototype->second.rule,{},tick};
    request.birthTicks=parameters.birthTicks;
    auto birth=prepareRoomSpawn(request,*loan.value,-1,{},false,collision);if(!birth) return {birth.status,{}};
    if(!(*birth.value)->actor()) {
        *loan.value=*(*birth.value)->data_->room;
        birth=prepareRoomSpawn(request,*loan.value,4,{},false,collision);if(!birth) return {birth.status,{}};
    }
    const auto born=(*birth.value)->actor();
    if(born) {
        auto &pet=(*birth.value)->data_->actors.actors.at(born);
        const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
        const int level=nativeStatValue(*stats,*owner->rule.stats,"level");
        pet.rule.noTreasure=true;pet.nativeRevive=true;pet.nativeMinionOwner=id;
        setStoredOwner(pet,id);pet.rule.level=level;
        attributes::MonsterResourceTrace resources(pet,tick);
        resources.writeBaseStat("level",level,{});
        auto values=resources.take();pet.nativeStats=std::move(values.intrinsic);
        pet.life=values.life.raw();pet.maximumLife=values.maximum.raw();
        pet.admissionRule=std::make_shared<const MonsterRule>(pet.rule);
        recordNativeMode(pet,parameters.nativeMode);pet.riseMode=parameters.nativeMode;
        pet.aiThinkAt=tick+20;++pet.aiThinkRevision;
        pet.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,born},FrameEventType::AiThink,pet.aiThinkAt}));
    }
    if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
    (*birth.value)->publish();return {DomainStatus::Applied,born};
}
}
