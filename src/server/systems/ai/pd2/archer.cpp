#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2ArcherAction(EntityId id,std::optional<UnitTarget> target,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto &program=*unit.rule.ai.pd2ArcherCycle;
    const auto values=ports_.effects.unitStatValues(id,tick.tick);
    if(!values) return StepStatus::Blocked;
    const auto stat=[&](std::string_view name){return nativeStatValue(*values,*unit.rule.stats,name);};
    const int phase=stat("mon_cooldown1"),alternate=stat("mon_cooldown2");
    const auto write=[&](std::vector<std::pair<std::string,int32_t>> writes) {
        return setPd2AiStats(controller,tick,std::move(writes));
    };
    const auto state=[&](bool active) {
        return perform(controller,[this,id,tick,active,state=program.state] {
            return ports_.monsters.toggleAiState(id,state,active,tick.tick);
        });
    };
    const auto failed=[](DomainResult<> result)->std::optional<StepStatus> {
        if(result) return {};
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    };
    const auto idle=[&] {
        const auto result=perform(controller,[this,id] {
            ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        });
        controller.pursuing=false;
        controller.nextDecision=tick.tick+uint64_t(unit.rule.ai.pd2FixedIdle);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    // 357330 changes real base resistances and state 8; its two clocks are
    // native attributes, so damage queries see the same phase as the AI.
    if(!phase) {
        if(const auto error=failed(write({{"fireresist",99},{"coldresist",99},{"lightresist",99},
                {"poisonresist",99},{"magicresist",50},{"damageresist",75}}))) return error;
        if(const auto error=failed(state(true))) return error;
    }
    if(phase>=program.attackEnd) {
        if(phase>=program.cycleEnd) {
            if(const auto error=failed(write({{"mon_cooldown1",0}}))) return error;
            return idle();
        }
        if(phase==program.attackEnd) {
            constexpr std::array names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
            std::vector<std::pair<std::string,int32_t>> writes;
            for(size_t index=0;index<names.size();++index) writes.emplace_back(names[index],program.restingResistances[index]);
            if(const auto error=failed(write(std::move(writes)))) return error;
            if(const auto error=failed(state(false))) return error;
        }
        if(const auto error=failed(write({{"mon_cooldown1",nativeAdd(phase,1)}}))) return error;
        return idle();
    }
    if(const auto error=failed(write({{"mon_cooldown1",nativeAdd(phase,1)},{"mon_cooldown2",alternate?0:1}}))) return error;
    if(alternate) {
        const auto skill=unit.rule.skillIds[1];const auto action=unit.rule.skillActions.find(skill);
        if(!skill || action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,target.value_or(UnitTarget{}),tick.tick,action->second.nativeMode});
    }
    // The preceding writes published the first loan. Native 39F570 and the
    // wander helper use ordinary D2 SEED, independently of PD2 event random.
    controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
    controller.randomPublished=false;
    if(int(limitedRandom(controller.random,100))>program.wanderChance) return {};
    const auto point=monsterWanderPoint(unit.position,program.wanderRadius,controller.random);
    const auto moved=perform(controller,[this,id,point,region=unit.area,generation=ports_.areas.at(unit.area).generation] {
        return ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
    });
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick;
    return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
