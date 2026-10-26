#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::trappedSoulFamily(EntityId id,TickContext tick,Controller &controller) {
    if(const auto result=pd2Mendeln(id,tick,controller)) return *result;
    if(const auto result=pd2DemonPillar(id,tick,controller)) return *result;
    const auto suppressed=ports_.monsters.suppressTreasure(id);
    if(!suppressed) return isDeferred(suppressed.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area).definition;
    const auto target=acquireTarget(unit,tick,false,false);
    uint8_t mode=8;
    if(target && target->distance<5) {
        if(!controller.phase) {
            controller.phase=1;controller.loop=nativeNarrow(uint32_t(tick.tick));mode=9;
        } else if(unitMeleeReach(area.collision,unit.position,unit.rule.size,target->position,target->size,unit.rule.meleeRange) &&
                  nativeNarrow(uint32_t(tick.tick))>controller.loop) {
            const auto x=int(std::floor(unit.position.x)),y=int(std::floor(unit.position.y));
            const auto tx=int(std::floor(target->position.x)),ty=int(std::floor(target->position.y));
            mode=x>=tx && y<=ty?4:x>tx || y<ty?8:5;
            controller.loop=nativeAdd(nativeNarrow(uint32_t(tick.tick)),mode==8?5:35);
        }
    } else if(!controller.phase) {
        ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+15;
        return StepStatus::Complete;
    }
    controller.nextDecision=tick.tick+1;
    const auto cast=requestAction(controller,{id,0,target?target->unit:UnitTarget{},tick.tick,mode});
    return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
