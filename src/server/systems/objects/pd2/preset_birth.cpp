#include "../system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"

namespace d2x::server::objects {
DomainResult<EntityId> System::preparePairedPreset(const Object &object,int definition,uint64_t tick,uint64_t &random,
    EntityIds &ids,monsters::NativeBirthBatch &births,std::optional<RoomRandomLoan> &roomRandom) {
    const auto &area=ports_.areas.at(object.area).definition;
    const auto prototype=area.nativeMonsterClasses.find(definition);
    if(prototype==area.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const auto global=object.position+area.origin;
    auto room=ports_.areas.roomAtPosition(object.area,object.position,int(global.x),int(global.y));
    if(!room) return {DomainStatus::Applied,EntityId{}};
    const auto sample=ports_.spatial.nativeCollisionSample();
    const auto collision=[&](RoomView owner,int32_t x,int32_t y) {
        auto mask=sample(owner,x,y);
        const int left=int(global.x)-object.rule.width/2,top=int(global.y)-object.rule.height/2;
        if(owner.area==object.area && object.rule.collision[size_t(object.mode)] &&
           x>=left && x<left+object.rule.width && y>=top && y<top+object.rule.height) mask|=object.rule.collisionMask;
        return mask;
    };
    const auto point=ports_.areas.nativeFreeSpace(*room,int(global.x),int(global.y),1,collision);
    if(!point) return {DomainStatus::Applied,EntityId{}};
    room=point->room;
    if(!roomRandom) {
        const auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
        roomRandom=*loan.value;
    }
    const auto &source=prototype->second;
    auto identity=source.identity;identity.origin=SpawnOrigin::Preset;identity.pd2Binding=0;
    identity.spawnKey="object.preset."+std::to_string(object.id.value);
    monsters::Admission request{identity,source.implementation,point->room.area,point->position,source.rule.alignment==0,source.rule,{},tick};
    request.resurrected=true;
    return ports_.monsters.stageRoomSpawn(request,*roomRandom,-1,ids,random,births,collision);
}
}
