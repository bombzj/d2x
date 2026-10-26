#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2Mendeln(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &map=ports_.maps.read();
    if(!unit.rule.stats || !unit.rule.stats->pd2 || unit.rule.nativeClass!=922 || !map.instance || map.instance->controller!=11) return {};
    const auto target=acquireTarget(unit,tick,false,false);
    const auto progress=map.eventProgress;
    const auto &params=unit.rule.ai.pd2Parameters;
    const int first=params[0][0]+1,second=first+params[0][1],last=second+params[0][2];
    const int iteration=int(progress)-1;
    const auto status=[](DomainResult<> result) {
        return isDeferred(result.status)?StepStatus::Blocked:result?StepStatus::Complete:StepStatus::NotImplemented;
    };
    const auto cast=[&](size_t slot,UnitTarget victim,std::optional<Vec> position={}) {
        const auto skill=unit.rule.skillIds[slot];const auto action=unit.rule.skillActions.find(skill);
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,victim,tick.tick,action->second.nativeMode,position});
    };
    controller.nextDecision=tick.tick+25;
    if(!progress) return StepStatus::Complete;
    if(progress==1) {
        if(!target || target->distance>=15) return StepStatus::Complete;
        if(!ports_.maps.advanceMendelnPhase(progress)) return StepStatus::Blocked;
        return cast(0,{},Vec{}-ports_.areas.at(unit.area).definition.origin);
    }
    if(iteration>=last) {
        if(iteration==last) {
            ports_.maps.advanceMendelnPhase(progress);controller.nextDecision=tick.tick+100;return StepStatus::Complete;
        }
        if(iteration==last+1) {
            ports_.maps.advanceMendelnPhase(progress);controller.nextDecision=tick.tick+25;
            return cast(1,{},Vec{}-ports_.areas.at(unit.area).definition.origin);
        }
        return status(perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick,{},true);}));
    }
    const auto consumed=ports_.monsters.consumeRandom(id,controller.randomExpected,controller.random);
    if(!consumed) return status(consumed);
    controller.randomPublished=true;
    const auto spawned=ports_.maps.mendelnWave(id,tick);if(!spawned) return status(spawned);
    controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;controller.randomPublished=false;
    const auto roll=pd2UnitEventRandom(controller.random)%3;
    controller.nextDecision=tick.tick+uint64_t(params[4][iteration>second?2:iteration>first?1:0]);
    if(!roll) return cast(2,target?target->unit:UnitTarget{});
    if(roll==1) return cast(0,{},unit.position+Vec{2,2});
    return StepStatus::Complete;
}
}
