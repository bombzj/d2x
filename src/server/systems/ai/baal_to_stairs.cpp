#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "gameplay/combat/geometry.hpp"

namespace d2x::server::ai {
StepStatus System::baalToStairsFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area).definition;
    const auto *room=area.activation.room(unit.position);
    const objects::Object *portal=nullptr;
    // AiUtil selector1 visits the authored adjacent-room list and each room's
    // unit chain. Fn138 accepts the first class563 within native distance25.
    if(room) for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data()))) {
        for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
            const auto found=ports_.objects.read().objects.find(candidate);
            if(found==ports_.objects.read().objects.end() || found->second.definition!=563) continue;
            const auto &object=found->second;
            const auto point=object.position+ports_.areas.at(object.area).definition.origin-area.origin;
            if(meleeDistance(unit.position,unit.rule.size,point,object.rule.width)<=25) {portal=&object;break;}
        }
        if(portal) break;
    }
    controller.nextDecision=tick.tick+25;
    if(!portal) {ports_.monsters.neutralMode(id);controller.pursuing=false;return StepStatus::Complete;}
    const auto point=portal->position+ports_.areas.at(portal->area).definition.origin-area.origin;
    if(meleeDistance(unit.position,unit.rule.size,point,portal->rule.width)>=unit.rule.ai.params[0]) {
        controller.nextDecision=tick.tick+uint64_t(unit.rule.neutralDelayTicks);
        const auto moved=perform(controller,[this,id,point] {
            const auto *actor=ports_.monsters.find(id);if(!actor || !actor->alive()) return DomainResult<>{DomainStatus::InvalidActor,{}};
            // AITACTICS_SetVelocity(1,0,0) chooses Toward path7 with base walk speed.
            return ports_.monsters.requestMove({id,{actor->area,ports_.areas.at(actor->area).generation,point},{},0,75,false,1,0,0,7,5});
        });
        controller.pursuing=bool(moved);return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto departed=perform(controller,[this,id]{return ports_.quests.openWorldstoneChamber(id);});
    return isDeferred(departed.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
