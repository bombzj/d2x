#include "system.hpp"
#include "server/area_store.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <utility>

namespace d2x::server::missiles {
std::vector<System::Admission> System::prepareTimers(const std::map<EntityId,Missile> &missiles) {
    std::vector<Admission> prepared;prepared.reserve(missiles.size());
    // MISSILES_Initialize deletes previous timers and registers native mode
    // event 0 in the continuous head. Entity IDs do not repeat in this game.
    for(const auto &[id,missile]:missiles) {
        const auto deadline=nativeEventDeadline(missile.created,-1);
        prepared.push_back({ports_.frameEvents.prepareInsertion({{},
            {FrameEventUnit::Missile,id},FrameEventType::ModeChange,deadline.due,true}),
            ports_.areas.prepareRoomUnit(id,3,missile.area,missile.position),missile.area,missile.position,
            missile.nativeDamage && missile.nativeDamage->rule.damageFormulas->missiles.at(missile.definition).number("collision")?
                missile.collision.size:0});
    }
    return prepared;
}
void System::publishTimers(std::vector<Admission> &prepared) noexcept {
    for(auto &entry:prepared) {
        ports_.areas.publishRoomUnit(*entry.room);ports_.areas.setMissileMask(entry.area,entry.position,entry.footprint,true);
        ports_.frameEvents.publish(std::move(entry.timer));
    }
}
}
