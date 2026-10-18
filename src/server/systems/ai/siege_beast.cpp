#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::ai {
bool System::chargeRayAvailable(const monsters::Actor &unit,Vec point) const {
    const auto &area=ports_.areas.at(unit.area);
    const int metric=monsterAiDistance(unit.position,unit.rule.size,point);
    const int offset=metric<3?2:metric<11?3:metric<25?4:3;
    const int dx=int(std::floor(unit.position.x))-int(std::floor(point.x)),dy=int(std::floor(unit.position.y))-int(std::floor(point.y));
    const int ox=((dx>0)-(dx<0))*offset,oy=((dy>0)-(dy<0))*offset;
    const auto free=[&](Vec at){return area.definition.collision.missileSegment(unit.position,at,{0x805,1});};
    return free(point) || free(point+Vec{float(-oy),float(ox)}) || free(point+Vec{float(oy),float(-ox)});
}
StepStatus System::siegeBeastFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    if(!ports_.monsters.find(unit.nativeOwner) && unit.alignment()==UnitAlignment::Evil) {
        const auto squared=[&](Vec at){const int x=int(unit.position.x)-int(at.x),y=int(unit.position.y)-int(at.y);return x*x+y*y;};
        bool assigned=false;
        if(const auto *room=area.definition.activation.room(unit.position))
            for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data()))) {
                for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *imp=ports_.monsters.find(candidate)) {
                    if(!imp->alive() || imp->rule.baseClass!=unit.rule.impBaseClass || imp->alignment()!=UnitAlignment::Evil) continue;
                    const auto at=imp->position+ports_.areas.at(imp->area).definition.origin-area.definition.origin;
                    if(squared(at)>params[0]*params[0]) continue;
                    auto [entry,fresh]=state_.controllers.try_emplace(candidate);auto &control=entry->second;
                    if(fresh) {control.actor=candidate;control.admissionRevision=imp->admissionRevision;control.observedAiFunctionRevision=imp->aiFunctionRevision;}
                    if(control.carrier) continue;
                    control.carrier=id;assigned=true;break;
                }
                if(assigned) break;
            }
    }
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto skill=unit.rule.skillIds[0];
    const auto cast=[&](bool stomp) {
        const auto result=requestAction(controller,{id,stomp?skill:uint16_t(0),stomp?UnitTarget{id,0,1}:target.value_or(UnitTarget{}),tick.tick,stomp?unit.rule.skillActions.at(skill).nativeMode:uint8_t(4)});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact) {
        if(skill && chance(params[2])) return cast(true);
        if(chance(params[1])) return cast(false);
        ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[3]);return StepStatus::Complete;
    }
    if(skill && distance<unit.rule.formulas->skills.at(skill).number("param5") && chance(params[4])) return cast(true);
    // Fn115's charge branch sets walking velocity. It does not cast the
    // declared Charge skill or choose that skill's movement program.
    const int bonus=target && chargeRayAvailable(unit,point) && chance(params[5])?std::clamp(params[6],0,127):0;
    const auto at=monsterRadiusApproachPoint(unit.position,unit.rule.size,point,12);
    const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,at},{},0,75+bonus,false,1});
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
}
}
