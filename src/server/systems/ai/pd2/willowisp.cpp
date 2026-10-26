#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2Willowisp(EntityId id,std::optional<UnitTarget> target,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto values=ports_.effects.unitStatValues(id,tick.tick);if(!values) return StepStatus::Blocked;
    const int cooldown=nativeStatValue(*values,*unit.rule.stats,"mon_cooldown1");
    const auto &params=unit.rule.ai.pd2Parameters;
    std::optional<Vec> spawn;
    std::optional<size_t> slot;
    UnitTarget victim=target.value_or(UnitTarget{});
    // These rolls precede publication of the stat write; writing a base stat
    // does not change either seed. Keep the wrapper's original short circuit.
    if(playerLifePercentage(unit.life,unit.maximumLife)<=params[3][2] &&
       int(limitedRandom(controller.random,100))<=params[4][2] && cooldown<1) {
        const int x=int(pd2UnitEventRandom(controller.random)&15)-8;
        const int y=int(pd2UnitEventRandom(controller.random)&15)-8;
        const auto proposed=unit.position+Vec{float(x),float(y)};
        const auto free=ports_.world.nativeFreeSpace(unit.area,unit.position,proposed,3);
        spawn=free && free->area==unit.area?free->position:proposed;
    } else {
        const auto &binding=ports_.maps.read().bindings[0];
        const auto *owner=binding.id!=id && !binding.object?ports_.monsters.find(binding.id):nullptr;
        if(owner && ports_.areas.hasRoomUnit(owner->id) && ports_.areas.hasRoomUnit(id)) {
            const auto a=unit.position+ports_.areas.at(unit.area).definition.origin;
            const auto b=owner->position+ports_.areas.at(owner->area).definition.origin;
            const int x=int(std::floor(a.x))-int(std::floor(b.x)),y=int(std::floor(a.y))-int(std::floor(b.y));
            if(nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y))>700) {slot=3;victim={owner->id,0,1};}
        }
        if(!slot && int(limitedRandom(controller.random,100))<=params[6][2]) slot=0;
        if(!slot && params[7][2] && cooldown<1 && int(limitedRandom(controller.random,100))<=params[7][2]) slot=2;
    }
    if(cooldown>0) {
        const auto write=setPd2AiStats(controller,tick,{{"mon_cooldown1",cooldown-1}});
        if(!write) return isDeferred(write.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    }
    if(spawn) {
        const auto result=perform(controller,[this,id,point=*spawn,tick]()->DomainResult<> {
            const auto born=ports_.monsters.spawnNativeNestChild(id,point,tick.tick,ports_.spatial.nativeCollisionSample(),{},-1,true);
            if(isDeferred(born.status)) return {born.status,{}};
            return {DomainStatus::Applied,std::monostate{}};
        });
        if(!result) return isDeferred(result.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        const auto write=setPd2AiStats(controller,tick,{{"mon_cooldown1",params[5][2]}});
        if(!write) return isDeferred(write.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        perform(controller,[this,id] {ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};});
        controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(unit.rule.ai.pd2FixedIdle);
        return StepStatus::Complete;
    }
    if(slot) {
        const auto skill=unit.rule.skillIds[*slot];const auto action=unit.rule.skillActions.find(skill);
        if(!skill || action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        const auto result=usePd2Skill(controller,{id,skill,victim,tick.tick,action->second.nativeMode});
        if(*slot==2) {
            const auto write=setPd2AiStats(controller,tick,{{"mon_cooldown1",5}});
            if(!write) return isDeferred(write.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        }
        return result;
    }
    if(controller.randomPublished) {
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
    }
    return {}; // Continue the unmodified Fn025 with this exact seed.
}
}
