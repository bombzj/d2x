#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/alignment.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::zakarumPriestFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;auto &random=controller.random;
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto chance=[&](int percent) {return int(limitedRandom(random,100))<percent;};
    const auto hasSkill=[&](size_t slot) {return unit.rule.skillIds[slot] && unit.rule.skillActions.contains(unit.rule.skillIds[slot]);};
    const bool clear=!target || area.definition.collision.missileSegment(unit.position,point,{4,1});
    const bool contact=target && clear && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange;
    const bool retaliating=unit.hitOccurrence!=controller.observedHit;controller.observedHit=unit.hitOccurrence;
    const auto cast=[&](int slot,UnitTarget victim,std::optional<Vec> coordinates={}) {
        const auto skill=slot<0?uint16_t(0):unit.rule.skillIds[size_t(slot)];
        const auto mode=slot<0?uint8_t(4):unit.rule.skillActions.at(skill).nativeMode;
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode,coordinates});
        controller.nextDecision=tick.tick+(result?1:10);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto primary=target.value_or(UnitTarget{});
    if(contact || retaliating) {
        if(hasSkill(2) && (unit.maximumLife?unit.life*100/unit.maximumLife:0)<33 && now>controller.phase) {
            controller.phase=nativeAdd(now,nativeMultiply(4,params[4]));
            const int scale=contact?4:1;
            const Vec destination{std::floor(point.x)+(std::floor(point.x)-std::floor(unit.position.x))*scale+.5f,
                std::floor(point.y)+(std::floor(point.y)-std::floor(unit.position.y))*scale+.5f};
            if(ports_.monsters.canNativeTeleport(id,destination,ports_.spatial.nativeCollisionSample(0x3c01)))
                return cast(2,{id,0,1},destination);
        }
        if(contact && chance(params[0])) return cast(-1,primary);
    }
    std::optional<UnitTarget> heal;
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                if(candidate==id || !other->alive() || !sameAlignment(unit.alignment(),other->alignment()) || other->alignment()==UnitAlignment::Good ||
                   (other->rule.baseClass!=unit.rule.baseClass && other->rule.ai.kind!=MonsterAiKind::ZakarumZealot)) continue;
                const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                const int x=int(std::floor(unit.position.x))-int(std::floor(at.x)),y=int(std::floor(unit.position.y))-int(std::floor(at.y));
                if(x*x+y*y>params[5]*params[5] || (other->maximumLife?other->life*100/other->maximumLife:0)>60) continue;
                // Retail 7BCF4 stores the target but never updates the
                // supposed minimum-life field: the last qualifying unit wins.
                heal=UnitTarget{candidate,0,1};
            }
    if(hasSkill(0) && heal && chance(25)) return cast(0,*heal);
    if(!clear) {
        if(chance(params[0]) && hasSkill(3) && now>controller.loop && chance(params[1])) {
            controller.loop=nativeAdd(now,params[4]);return cast(3,primary);
        }
    } else if(chance(params[3])) {
        if(hasSkill(3) && now>controller.loop && chance(params[1])) {
            controller.loop=nativeAdd(now,params[4]);return cast(3,primary);
        }
        if(hasSkill(1) && now>controller.thirdParameter && chance(params[2])) {
            controller.thirdParameter=nativeAdd(now,20);return cast(1,primary);
        }
    }
    if(chance(30)) {
        const int turn=(rollRandom(random)&255)<128?1:-1;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,0,turn,4});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else {
        ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+20;
    }
    return StepStatus::Complete;
}
}
