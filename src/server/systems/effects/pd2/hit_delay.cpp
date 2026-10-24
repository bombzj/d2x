#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"

namespace d2x::server::effects {
bool System::pd2MissileHitAllowed(EntityId target,const std::array<int32_t,2> &key) const {
    const FrameEventOwner owner{ports_.players.findActor(target)?FrameEventUnit::Player:FrameEventUnit::Monster,target};
    for(const auto &event:ports_.frameEvents.events(owner))
        if(event.type==FrameEventType::Pd2MissileHitDelay && event.customId==key[0] && event.customParameter==key[1]) return false;
    return true;
}
DomainResult<> System::pd2MissileHitDelay(EntityId target,const std::array<int32_t,2> &key,uint64_t duration,uint64_t tick) {
    if(!ports_.players.findActor(target) && !ports_.monsters.find(target)) return {DomainStatus::InvalidActor,{}};
    if(duration) {
        auto timer=ports_.frameEvents.prepareInsertion({{}, {ports_.players.findActor(target)?FrameEventUnit::Player:FrameEventUnit::Monster,target},
            FrameEventType::Pd2MissileHitDelay,tick+duration,false,0,key[0],key[1]});
        ports_.frameEvents.publish(std::move(timer));
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
