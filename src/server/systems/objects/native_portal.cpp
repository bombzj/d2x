#include "system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/maps/system.hpp"

namespace d2x::server::objects {
void System::restoreNativePortalReferences(EntityId previous,EntityId current) {
    if(!previous || previous==current) return;
    for(auto &[id,object]:state_.objects) {
        (void)id;
        if(object.nativePortalOwner==previous) {object.nativePortalOwner=current;++object.revision;}
    }
    for(auto &[room,objects]:inactive_) {
        (void)room;
        for(auto &object:objects) if(object.nativePortalOwner==previous) object.nativePortalOwner=current;
    }
    ports_.maps.objectRestored(previous,current);
}
DomainResult<> System::setNativeMode(EntityId id,int mode,uint64_t tick,bool animate) {
    const auto found=state_.objects.find(id);if(found==state_.objects.end()) return {DomainStatus::Stale,{}};
    auto &object=found->second;
    if(mode<0 || mode>=8) return {DomainStatus::InvalidRequest,{}};
    if(object.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    object.mode=mode;object.until=animate?tick+object.rule.openingTicks:0;
    ++object.revision;collision(object.area);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::linkNativePortals(EntityId source,EntityId destination,bool mutual,uint64_t tick) {
    const auto first=state_.objects.find(source),second=state_.objects.find(destination);
    if(first==state_.objects.end() || second==state_.objects.end()) return {DomainStatus::Stale,{}};
    auto &from=first->second;auto &to=second->second;
    if(source==destination || int(from.area)<1 || int(from.area)>255 || int(to.area)<1 || int(to.area)>255 ||
        ports_.areas.at(from.area).definition.act!=ports_.areas.at(to.area).definition.act) return {DomainStatus::InvalidRequest,{}};
    if(from.revision==UINT64_MAX || to.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    from.nativePortalOwner=destination;if(mutual) to.nativePortalOwner=source;
    from.portalDestination=uint8_t(int(to.area));to.portalDestination=uint8_t(int(from.area));
    from.mode=to.mode=1;from.until=tick+from.rule.openingTicks;to.until=tick+to.rule.openingTicks;
    ++from.revision;++to.revision;
    ports_.world.retainControllerUnitRoom(source,from.area,from.position,true);
    ports_.world.retainControllerUnitRoom(destination,to.area,to.position,true);
    collision(from.area);if(to.area!=from.area) collision(to.area);
    return {DomainStatus::Applied,std::monostate{}};
}
}
