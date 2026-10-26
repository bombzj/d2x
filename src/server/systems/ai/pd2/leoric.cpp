#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <utility>

namespace d2x::server::ai {
StepStatus System::pd2Leoric(EntityId id,UnitTarget target,Vec point,int size,int distance,
                           TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto &area=ports_.areas.at(unit.area);
    const auto values=ports_.effects.unitStatValues(id,tick.tick);
    if(!values) return StepStatus::Complete;
    // Current core 17d5, RVA 35D820: exactly one unit-seed draw, including
    // the summon branch. The reset reuses its remainder, not a second draw.
    const int roll=int(pd2UnitEventRandom(controller.random)%100);
    const int cooldown=nativeStatValue(*values,*unit.rule.stats,"mon_cooldown1");
    const int next=cooldown<1?roll%10+15:cooldown-1;
    const auto written=setPd2AiStats(controller,tick,{{"mon_cooldown1",next}});
    if(!written) return isDeferred(written.status)?StepStatus::Blocked:StepStatus::Complete;
    const auto cast=[&](int slot,UnitTarget victim,std::optional<Vec> position={},int delay=1) {
        const auto skill=slot<0?uint16_t{}:unit.rule.skillIds[size_t(slot)];
        const auto mode=slot<0?uint8_t{4}:unit.rule.skillActions.at(skill).nativeMode;
        if(slot>=0) return usePd2Skill(controller,{id,skill,victim,tick.tick,mode,position},delay==1?std::nullopt:std::optional{delay},delay!=1);
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode,position});
        controller.nextDecision=tick.tick+uint64_t(delay);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(cooldown<1) return cast(0,target);
    const auto *effects=ports_.effects.unitEffects(id);
    const bool armor=effects && effects->hasState(14,tick.tick);
    if(roll<20 && !armor) return cast(2,{id,0,1});
    const bool contact=meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(roll<60) {
        if(distance>15) return cast(3,target);
        // The wrapper receives a null unit and the target's sampled X/Y.
        // Keep the 30-frame AI event even when starting the charge fails.
        if(roll<30 && distance<10) return cast(1,{},point,30);
        if(contact) return cast(4,target);
    }
    if(contact) return cast(-1,target);
    const auto moved=perform(controller,[this,id,region=unit.area,generation=area.generation,point,target] {
        return ports_.monsters.requestMove({id,{region,generation,point},target.id,0,100,false,1});
    });
    controller.pursuing=bool(moved);
    controller.nextDecision=tick.tick+uint64_t(unit.rule.neutralDelayTicks);
    return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
