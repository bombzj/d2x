#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2DemonPillar(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    if(!unit.rule.stats || !unit.rule.stats->pd2 || unit.rule.nativeClass!=1057) return {};
    const bool arena=int(unit.area)==182;
    int pressure=1;
    if(arena) {
        const auto &binding=ports_.maps.read().bindings[0];
        const auto *owner=!binding.object?ports_.monsters.find(binding.id):nullptr;
        // A missing/wrong native owner delegates Fn099; another monster of
        // the same class elsewhere in the level cannot stand in for it.
        if(!owner || owner->identity.nativeClass!=1060) return {};
        const auto life=uint64_t(uint32_t(owner->life)),maximum=uint64_t(uint32_t(owner->maximumLife));
        pressure=!maximum || life*100<maximum*50?2:life*100<maximum*75?1:0;
    }
    const auto target=acquireTarget(unit,tick,false,false);
    const auto victim=target?target->unit:UnitTarget{};
    const auto difficulty=size_t(unit.rule.difficulty);
    const auto &params=unit.rule.ai.pd2Parameters;
    const auto failed=[](DomainResult<> result)->std::optional<StepStatus> {
        if(result) return {};
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    };
    const auto targetable=[&](bool enabled) {
        return failed(perform(controller,[this,id,enabled] {
            ports_.monsters.monsterTargetable(id,enabled);
            return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        }));
    };
    const auto cast=[&](size_t slot) {
        const auto skill=unit.rule.skillIds[slot];const auto action=unit.rule.skillActions.find(skill);
        if(!skill || action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,victim,tick.tick,action->second.nativeMode});
    };
    const auto clock=nativeNarrow(uint32_t(tick.tick));
    if(controller.loop) {
        if(unit.nativeMode==1) return cast(1);
        if(const auto error=targetable(true)) return error;
        if(clock<=controller.thirdParameter) return cast(0);
        if(arena && controller.loop==2) {
            controller.loop=1;
            const auto result=cast(2);
            controller.thirdParameter=nativeAdd(clock,params[3][difficulty]+13);
            return result;
        }
        // Flags are not a random operation, but publishing them may have
        // committed a prior seed loan. Continue from the live unit seed.
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
        const int roll=int(limitedRandom(controller.random,100));
        if(roll<=params[2][difficulty] || (!arena && controller.loop==2)) {
            const size_t slot=arena && pd2CrtRandom(ports_.pd2Random)%100<40?2:3;
            const auto result=cast(slot);
            controller.thirdParameter=nativeAdd(clock,params[3][difficulty]+(arena?13:15));
            if(!arena) controller.loop=1;
            return result;
        }
        const auto result=cast(0);
        controller.thirdParameter=nativeAdd(clock,unit.rule.ai.pd2Delays[difficulty]);
        return result;
    }
    if(const auto error=targetable(false)) return error;
    const bool near=target && target->distance<params[1][difficulty];
    if(pressure==2 || (pressure==1 && near)) {
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
        if(int(limitedRandom(controller.random,100))<=params[0][difficulty]) {
            const auto result=cast(1);controller.loop=2;return result;
        }
    }
    const auto idle=perform(controller,[this,id] {
        ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
    });
    controller.thirdParameter=nativeAdd(clock,11);
    return failed(idle).value_or(StepStatus::Complete);
}
}
