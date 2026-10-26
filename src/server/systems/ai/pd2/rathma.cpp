#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "gameplay/combat/life.hpp"

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2RathmaSpire(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &map=ports_.maps.read();
    if(unit.identity.nativeClass!=966 || !map.instance || map.instance->controller!=43) return {};
    const auto &area=ports_.areas.at(unit.area).definition;
    const monsters::Actor *owner=nullptr;
    // 356660 walks room-neighbor unit lists and returns the first 934.
    // This helper deliberately does not use the controller's owner slots.
    if(const auto *room=area.activation.room(unit.position))
        for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data()))) {
            for(const auto candidate:ports_.areas.roomUnitOrder(near))
                if(const auto *other=ports_.monsters.find(candidate);other && other->id!=id && other->identity.nativeClass==934) {
                    owner=other;break;
                }
            if(owner) break;
        }
    const int delay=owner && map.pauseDepth>0?10:25;
    if(owner && map.pauseDepth<=0) {
        if(!ports_.areas.hasRoomUnit(id)) return StepStatus::Complete;
        const int life=playerLifePercentage(owner->life,owner->maximumLife);
        if(life<16) {
            const auto killed=perform(controller,[this,id,tick] {return ports_.monsters.scriptDeath(id,tick.tick,{},true);});
            return isDeferred(killed.status)?StepStatus::Blocked:killed?StepStatus::Complete:StepStatus::NotImplemented;
        }
        if(life<91) return {};
    }
    const auto idle=perform(controller,[this,id] {
        ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
    });
    controller.nextDecision=tick.tick+uint64_t(delay);
    return isDeferred(idle.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
