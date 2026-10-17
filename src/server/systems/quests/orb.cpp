#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/world/system.hpp"
#include <stdexcept>

namespace d2x::server::quests {
StepStatus System::orbStep(TickContext tick) {
    auto &state=state_.actThree;if(!state.orbFxPending) return StepStatus::Complete;
    const monsters::Actor *orb=nullptr;
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        (void)id;if(int(unit.area)==83 && unit.questNpc && unit.identity.monster=="compellingorb") {orb=&unit;break;}
    }
    if(!orb) {state.orbFxPending=false;return StepStatus::Complete;}
    const auto &area=ports_.areas.at(orb->area);
    const auto *origin=area.definition.activation.room(orb->position);
    if(!origin) throw std::logic_error("Compelling Orb has no native room");
    const objects::Object *stairs=nullptr;
    for(const auto room:area.definition.roomNear.at(size_t(origin-area.definition.activation.rooms().data()))) {
        for(const auto id:ports_.areas.roomUnitOrder(room)) {
            const auto found=ports_.objects.read().objects.find(id);
            if(found!=ports_.objects.read().objects.end() && found->second.definition==386) {stairs=&found->second;break;}
        }
        if(stairs) break;
    }
    if(!stairs) throw std::logic_error("Missing original Compelling Orb stairs");
    const auto death=ports_.monsters.scriptDeath(orb->id,tick.tick);
    if(!death) return StepStatus::Blocked;
    const auto definition=ports_.missiles.nativeDefinition("orbmist");
    if(!definition) throw std::logic_error("Missing original Orb Mist missile");
    const auto target=stairs->position+ports_.areas.at(stairs->area).definition.origin-area.definition.origin;
    const auto missile=ports_.missiles.spawnNative({orb->id,orb->area,area.generation,tick.tick,orb->position,target,*definition,0,1,
        {int32_t(uint32_t(stairs->id.value)),0},true});
    if(isDeferred(missile.status)) return StepStatus::Blocked;
    if(missile) ports_.world.retainControllerRoom(stairs->area,stairs->position,true);
    else if(!ports_.objects.openOrbMist(stairs->id,tick.tick)) return StepStatus::Blocked;
    state.orbFxPending=false;return StepStatus::Complete;
}
}
