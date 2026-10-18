#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::evilHoleFamily(EntityId id,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &params=unit.rule.ai.params;
    const int32_t now=nativeNarrow(uint32_t(tick.tick));
    const auto deadline=[&] {controller.phase=nativeAdd(now,params[1]);};
    const auto idle=[&](int frames) {controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));};
    const auto pose=[&](uint8_t mode,int frames) {
        const auto changed=perform(controller,[this,id,mode,frames,tick]{return ports_.monsters.aiPose(id,mode,true,false,tick.tick,frames);});
        idle(frames);return isDeferred(changed.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(controller.phase<=0) {deadline();controller.loop=params[0];}
    if(unit.nativeMode==1) {
        if(distance>5) {ports_.monsters.neutralMode(id);idle(5);return StepStatus::Complete;}
        return pose(10,20);
    }
    if(unit.nativeMode==10) return pose(11,20);
    if(unit.nativeMode==11 && controller.loop>0) {
        if(now<=controller.phase) {idle(params[1]);return StepStatus::Complete;}
        deadline();idle(params[1]);
        const auto spawned=perform(controller,[this,id,tick]()->DomainResult<> {
            const auto result=ports_.monsters.spawnEvilHoleChild(id,tick.tick,ports_.spatial.nativeCollisionSample());
            if(result.status==DomainStatus::Stale || result.status==DomainStatus::Conflict) return {DomainStatus::Capacity,{}};
            if(!result) return {result.status,{}};
            if(*result.value) if(const auto current=state_.controllers.find(id);current!=state_.controllers.end()) --current->second.loop;
            return {DomainStatus::Applied,std::monostate{}};
        });
        return isDeferred(spawned.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto dead=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
    return isDeferred(dead.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
