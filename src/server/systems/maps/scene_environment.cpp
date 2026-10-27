#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/monsters/pd2/boss_program.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::removeSceneWaypoints() {
    const auto &rooms=ports_.travel.read().pd2SceneEntryRooms;
    if(!state_.instance || state_.instance->controller<41 || state_.instance->controller>45 || rooms.empty())
        return {DomainStatus::Applied,std::monostate{}};
    std::vector<EntityId> remove;
    // 38A680 walks the destination room's native unit chain. Re-read it on
    // restoration instead of marking a room permanently scanned.
    for(const auto &room:rooms) for(const auto id:ports_.areas.roomUnitOrder(room))
        if(const auto object=ports_.objects.read().objects.find(id);
           object!=ports_.objects.read().objects.end() && object->second.rule.operation==23)
            remove.push_back(id);
    std::sort(remove.begin(),remove.end());
    remove.erase(std::unique(remove.begin(),remove.end()),remove.end());
    for(const auto id:remove) {const auto removed=ports_.objects.removeNative(id);if(!removed) return removed;}
    return {DomainStatus::Applied,std::monostate{}};
}
void System::sceneRoomRelease(RegionId region,size_t index) {
    if(!state_.instance || state_.pauseDepth!=0) return;
    const auto *area=ports_.areas.find(region);if(!area || index>=area->definition.activation.rooms().size()) return;
    for(const auto &binding:state_.bindings) if(binding.id && !binding.object && binding.area==region) {
        const auto *unit=ports_.monsters.find(binding.id);if(!unit) continue;
        const auto *room=area->definition.activation.room(unit->position);
        if(room && size_t(room-area->definition.activation.rooms().data())==index && pd2::scenePauseClass(unit->identity.nativeClass)) {
            state_.pauseDepth=15;return;
        }
    }
}
DomainResult<> System::sceneEclipse(RegionId area,bool enabled,TickContext tick) {
    const auto *region=ports_.areas.find(area);if(!region) return {DomainStatus::Unavailable,{}};
    const auto act=region->definition.act;
    const auto sent=ports_.events.publishGroup({
        {0,tick.tick,{}, {AudienceKind::Game,{},{}},{EnvironmentFact{act,act,5,enabled}}},
        {0,tick.tick,{}, {AudienceKind::Area,{},area},{SoundFact{{},2,area,0,true}}}});
    if(!sent) return {sent.status,{}};
    ports_.world.setActEclipse(act,enabled);return {DomainStatus::Applied,std::monostate{}};
}
}
