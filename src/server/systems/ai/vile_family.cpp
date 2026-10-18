#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <array>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::vileFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto idle=[&](int frames){ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    const auto walk=[&] {
        const auto move=[&](Vec at,EntityId victim){return ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,75,false,1});};
        auto moved=move(point,target?target->id:EntityId{});
        if(!moved) {
            if(limitedRandom(controller.random,100)>=70) {idle(10);return;}
            moved=move(monsterWanderPoint(unit.position,4,controller.random),{});
        }
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    };
    const auto attack=[&] {
        const auto result=requestAction(controller,{id,0,target.value_or(UnitTarget{}),tick.tick,4});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(unit.rule.ai.kind==MonsterAiKind::VileDog) {
        if(!controller.phase) {controller.phase=1;idle(5);return StepStatus::Complete;}
        if(contact) {if(chance(params[0])) return attack();idle(params[1]);}
        else if(chance(params[2])) walk();else idle(10);
        return StepStatus::Complete;
    }
    if(controller.phase<params[0] && unit.alignment()==UnitAlignment::Evil && chance(params[2]) && unit.rule.nestChild) {
        int children=0;
        if(const auto *room=area.definition.activation.room(unit.position))
            for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
                for(const auto child:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(child)) {
                    if(!other->alive() || other->rule.nativeClass!=unit.rule.nestChild->rule.nativeClass) continue;
                    const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                    if(monsterAiDistance(unit.position,unit.rule.size,at)<=25) ++children;
                }
        if(children<params[1]) {
            constexpr std::array indices{6,4,2,2,2,0,6,6};
            const int facing=monsterFacing8(unit.position,point);
            const auto collision=ports_.spatial.nativeCollisionSample(0x3c01);
            for(int attempt=0;attempt<8;++attempt) {
                const auto at=unit.position+monsterDirectionOffset(indices[size_t((facing+attempt)%8)])*3.f;
                const auto global=at+area.definition.origin;
                const auto room=ports_.areas.roomAtPosition(unit.area,unit.position,int(std::floor(global.x)),int(std::floor(global.y)));
                const auto skill=unit.rule.skillIds[0];
                if(!skill || !room || ports_.areas.nativeCollisionMask(*room,int(std::floor(global.x)),int(std::floor(global.y)),2,0x3c01,collision)) continue;
                const auto result=requestAction(controller,{id,skill,UnitTarget{id,0,1},tick.tick,unit.rule.skillActions.at(skill).nativeMode,at});
                // Native birth-choice returns success after UseSkill even if
                // the mode start fails: attempts, rather than births, count.
                ++controller.phase;controller.nextDecision=tick.tick+(result?1:10);
                return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
            }
        }
    }
    if(contact) {if(chance(params[3])) return attack();idle(params[6]);return StepStatus::Complete;}
    if(controller.phase>=params[0] || chance(params[4])) {walk();return StepStatus::Complete;}
    if(chance(params[5])) {
        const int turn=(rollRandom(controller.random)&255)<128?1:-1;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1,turn,4});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else idle(15);
    return StepStatus::Complete;
}
}
