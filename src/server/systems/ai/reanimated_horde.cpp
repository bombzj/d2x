#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::reanimatedHordeFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    ports_.monsters.monsterTargetable(id,true);
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto attack=[&](uint16_t skill,uint8_t mode) {
        const auto result=requestAction(controller,{id,skill,target.value_or(UnitTarget{}),tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact) {if(chance(params[0])) return attack(0,4);idle(params[1]);return StepStatus::Complete;}
    // Fn114 tests three rays before its distance/chance gates. One free ray
    // suffices; the lateral endpoints are coordinate probes, not targets.
    const auto skill=unit.rule.skillIds[1];
    const bool chargeRay=skill && target && chargeRayAvailable(unit,point);
    if(chargeRay && distance<params[2] && distance>5 && chance(params[3]))
        return attack(skill,unit.rule.skillActions.at(skill).nativeMode);
    if(chance(params[4])) {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else if(chance(params[5])) {
        const auto at=monsterRadiusApproachPoint(unit.position,unit.rule.size,point,4);
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,at},{},0,75,false,1});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else idle(params[6]);
    return StepStatus::Complete;
}
}
