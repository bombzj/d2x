#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"

namespace d2x::server::ai {
StepStatus System::putridDefilerFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto contact=[&](Vec at,int extent){return meleeDistance(unit.position,unit.rule.size,at,extent)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,at,{4,1});};
    const auto cast=[&](uint16_t skill,uint8_t mode,UnitTarget victim) {
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});controller.nextDecision=tick.tick+(result?1:10);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(target && contact(point,size)) return cast(0,4,*target);
    const monsters::Actor *recipient=nullptr;Vec position;
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data()))) {
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(ports_.monsters.canImpregnate(id,candidate,tick.tick)) {
                const auto *other=ports_.monsters.find(candidate);
                const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                if(monsterAiDistance(unit.position,unit.rule.size,at)<=25) {recipient=other;position=at;break;}
            }
            if(recipient) break;
        }
    if(recipient && contact(position,recipient->rule.size)) {
        const auto skill=unit.rule.skillIds[0];return cast(skill,8,{recipient->id,0,1});
    }
    if(recipient || distance<params[0]) {
        const auto at=recipient?position:monsterRetreatPoint(unit.position,point,uint8_t(params[1]));
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,at},recipient?recipient->id:EntityId{},0,75,false,1,0,0,13,uint8_t(recipient?5:params[1])});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+25;}
    return StepStatus::Complete;
}
}
