#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::baalMinionFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    DomainResult<> result{DomainStatus::Applied,std::monostate{}};
    if(contact) {
        if(chance(params[0])) {
            const auto skill=unit.rule.skillIds[0];const auto used=skill && chance(params[2])?skill:uint16_t(0);
            result=requestAction(controller,{id,used,*target,tick.tick,used?unit.rule.skillActions.at(used).nativeMode:uint8_t(4)});
        } else {ports_.monsters.stop(id);controller.pursuing=false;}
    } else if(chance(params[1])) {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1});
        controller.pursuing=bool(moved);
    }
    // Fn141 always ends with Idle (not IdleInNeutralMode). This replaces the
    // prior AI timer without cancelling the attack or movement just started.
    controller.nextDecision=tick.tick+uint64_t(params[3]);
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
