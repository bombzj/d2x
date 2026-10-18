#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::suicideMinionFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto idle=[&]{ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[1]);};
    if(controller.phase) {
        if(now>controller.phase) {
            const auto killed=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
            return isDeferred(killed.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        idle();return StepStatus::Complete;
    }
    if(target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1})) {
        // Retail reads aip5, not the neighbouring radius field aip4.
        controller.phase=nativeAdd(now,params[4]);idle();return StepStatus::Complete;
    }
    if(int(limitedRandom(controller.random,100))>=params[2]) {idle();return StepStatus::Complete;}
    const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1});
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
}
}
