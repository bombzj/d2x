#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace d2x::server::missiles {
std::vector<EntityId> System::nearbyRoomUnits(const Missile &missile,std::optional<Vec> at) const {
    const auto *area=ports_.areas.find(missile.area);if(!area) return {};
    // Movement recaches the missile room before HandleMissileCollision. The
    // previous live position is only the anchor for the native near-room search.
    Vec anchor=missile.position;
    if(const auto live=state_.missiles.find(missile.id);live!=state_.missiles.end() && live->second.area==missile.area) anchor=live->second.position;
    const auto global=at.value_or(missile.position)+area->definition.origin;
    const auto found=ports_.areas.roomAtPosition(missile.area,anchor,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!found) return {};
    area=ports_.areas.find(found->area);if(!area) return {};
    const auto &definition=area->definition;const auto &rooms=definition.activation.rooms();
    const auto *room=definition.activation.room(global-definition.origin);if(!room) return {};
    const auto &views=definition.roomNear.at(size_t(room-rooms.data()));
    std::vector<EntityId> result;std::set<EntityId> visited;
    for(const auto view:views) {
        const auto *target=ports_.areas.find(view.area);if(!target || target->definition.act!=definition.act) continue;
        for(const auto id:ports_.areas.roomUnitOrder(view)) {
            if(visited.insert(id).second) result.push_back(id);
        }
    }
    return result;
}
std::vector<EntityId> System::nearbyUnits(const Missile &missile,std::optional<Vec> at) const {
    auto result=nearbyRoomUnits(missile,at);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    std::erase_if(result,[&](EntityId id) {
        const auto unit=participants.find(id);if(!unit.id()) return true;
        const auto region=unit.player?unit.player->area:unit.monster->area;
        return !unit.aliveIn(region) || !ports_.areas.loaded(region,unit.position());
    });
    return result;
}
RegionId System::unitArea(EntityId id) const {
    if(const auto *player=ports_.players.findActor(id)) return player->area;
    if(const auto *unit=ports_.monsters.find(id)) return unit->area;
    throw std::logic_error("Missing missile contact unit");
}
std::optional<Vec> System::unitPosition(const Missile &missile,EntityId id) const {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto unit=participants.find(id);if(!unit.id()) return {};
    const auto region=unitArea(id);if(!unit.aliveIn(region) || !ports_.areas.loaded(region,unit.position())) return {};
    const auto *source=ports_.areas.find(missile.area),*target=ports_.areas.find(region);
    if(!source || !target || source->definition.act!=target->definition.act) return {};
    return unit.position()+target->definition.origin-source->definition.origin;
}
bool System::moveToRoom(Missile &missile,Vec anchor) const {
    const auto *source=ports_.areas.find(missile.area);if(!source) return false;
    const auto global=missile.position+source->definition.origin;
    const auto room=ports_.areas.roomAtPosition(missile.area,anchor,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room) return false;
    const auto &target=ports_.areas.at(room->area);
    const auto shift=source->definition.origin-target.definition.origin;
    missile.position=missile.motion.translate(missile.position,shift);
    missile.pathTarget=missile.pathTarget+shift;
    for(auto &point:missile.path) point=point+shift;
    missile.area=room->area;missile.generation=target.generation;
    return true;
}
bool System::creationRoom(Missile &missile) const {
    if(missile.velocity.length()>0 &&
        (std::abs(int32_t(std::floor(missile.pathTarget.x))-int32_t(std::floor(missile.position.x)))>=100 ||
         std::abs(int32_t(std::floor(missile.pathTarget.y))-int32_t(std::floor(missile.position.y)))>=100)) return false;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(missile.owner);if(!owner.id()) return false;
    const auto *source=ports_.areas.find(missile.area);const auto region=unitArea(owner.id());
    const auto *target=ports_.areas.find(region);
    if(!source || !target || source->definition.act!=target->definition.act) return false;
    const auto shift=source->definition.origin-target->definition.origin;
    missile.position=missile.motion.translate(missile.position,shift);missile.pathTarget=missile.pathTarget+shift;for(auto &point:missile.path) point=point+shift;
    missile.area=region;missile.generation=target->generation;
    // MISSILES_CreateMissileFromParams searches the actual owner's current
    // room and its neighbours. A distant parent cannot invent a loaded room.
    return moveToRoom(missile,owner.position());
}
bool System::townAllowed(const Missile &missile) const {
    const auto *area=ports_.areas.find(missile.area);if(!area || !missile.nativeDamage) return false;
    const auto &rule=missile.nativeDamage->rule;
    if(area->definition.town && !rule.town) return false;
    if(rule.sourceTown) {
        const auto *player=ports_.players.findActor(missile.owner);const auto *unit=ports_.monsters.find(missile.owner);
        const auto *ownerArea=player?ports_.areas.find(player->area):unit?ports_.areas.find(unit->area):nullptr;
        if(ownerArea && ownerArea->definition.town) return false;
    }
    return true;
}
}
