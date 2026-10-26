#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <cmath>
#include <memory>
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::usePd2Skill(Controller &controller,const actions::MonsterCastRequest &request,std::optional<int> wait,bool waitAfter) {
    const auto result=perform(controller,[this,request,wait,waitAfter]()->DomainResult<> {
        const auto cast=ports_.skills.requestCast(request);
        if(isDeferred(cast.status) || cast.status==DomainStatus::NotImplemented) return cast;
        auto &control=state_.controllers.at(request.actor);
        if(!cast) ports_.monsters.neutralMode(request.actor);
        control.nextDecision=request.tick+uint64_t(cast?0:10);
        if(wait && (cast || waitAfter)) control.nextDecision=request.tick+uint64_t(std::max(1,*wait));
        // AITACTICS_UseSkill's native failure is consumed by the wrapper.
        // Later counter writes still run, including after a deferred attempt.
        return {DomainStatus::Applied,std::monostate{}};
    });
    return isDeferred(result.status)?StepStatus::Blocked:result.status==DomainStatus::NotImplemented?StepStatus::NotImplemented:StepStatus::Complete;
}
DomainResult<> System::walkToTargetWithFallback(Controller &controller,UnitTarget target,Vec point,TickContext tick) {
    struct Walk {bool attempted{};std::optional<Vec> fallback;};
    auto walk=std::make_shared<Walk>();
    return perform(controller,[this,id=controller.actor,target,point,tick,walk]()->DomainResult<> {
        const auto *unit=ports_.monsters.find(id);
        if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        const auto &area=ports_.areas.at(unit->area);
        auto &control=state_.controllers.at(id);
        if(!walk->attempted) {
            const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,point},target.id,0,75,false,1});
            if(isDeferred(moved.status)) return moved;
            if(moved) {control.pursuing=true;control.nextDecision=tick.tick;return moved;}
            const auto expected=ports_.monsters.find(id)->combatRandom;auto random=expected;
            const bool pause=limitedRandom(random,100)>=70;
            const auto fallback=pause?std::optional<Vec>{}:std::optional{monsterWanderPoint(unit->position,4,random)};
            const auto consumed=ports_.monsters.consumeRandom(id,expected,random);
            if(!consumed) return consumed;
            walk->attempted=true;walk->fallback=fallback;
        }
        if(!walk->fallback) {
            ports_.monsters.stop(id);control.pursuing=false;control.nextDecision=tick.tick+10;
            return {DomainStatus::Applied,std::monostate{}};
        }
        const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,*walk->fallback},{},0,75,false,1});
        if(isDeferred(moved.status)) return moved;
        control.pursuing=bool(moved);control.nextDecision=tick.tick;
        return {DomainStatus::Applied,std::monostate{}};
    });
}
DomainResult<> System::setPd2AiStats(Controller &controller,TickContext tick,std::vector<std::pair<std::string,int32_t>> writes) {
    return perform(controller,[this,id=controller.actor,tick,writes=std::move(writes)]()->DomainResult<> {
        const auto *unit=ports_.monsters.find(id);
        if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        const auto sources=ports_.effects.unitStatSources(id,tick.tick);
        attributes::MonsterResourceTrace trace(*unit,tick.tick,ports_.effects.unitEffects(id));
        trace.sampleSources(sources);
        for(const auto &[name,value]:writes) trace.writeBaseStat(name,value,sources);
        auto change=trace.take();
        if(!ports_.monsters.canCommitLife(change)) return {DomainStatus::Stale,{}};
        return ports_.monsters.commitLife(std::move(change))?DomainResult<>{DomainStatus::Applied,std::monostate{}}:
            DomainResult<>{DomainStatus::Stale,{}};
    });
}
DomainResult<> System::setPd2Guard(Controller &controller,TickContext tick,bool active) {
    const auto *unit=ports_.monsters.find(controller.actor);if(!unit) return {DomainStatus::InvalidActor,{}};
    constexpr std::array names{"fireresist","lightresist","coldresist","poisonresist","magicresist","damageresist"};
    std::vector<std::pair<std::string,int32_t>> writes;
    for(size_t index=0;index<names.size();++index)
        writes.emplace_back(names[index],active?200:unit->rule.ai.pd2RestingResistances[index]);
    const auto written=setPd2AiStats(controller,tick,std::move(writes));if(!written) return written;
    const auto state=perform(controller,[this,id=controller.actor,active,tick] {
        return ports_.monsters.toggleAiState(id,8,active,tick.tick);
    });
    if(!state || !active) return state;
    for(const int state:{2,62}) {
        const auto removed=perform(controller,[this,id=controller.actor,state,tick] {
            return ports_.effects.removeUnitState(id,state,tick.tick,20);
        });
        if(!removed) return removed;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
std::optional<StepStatus> System::pd2SpecialAction(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,
                                                TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    if(!target) point=Vec{}-ports_.areas.at(unit.area).definition.origin;
    if(unit.rule.ai.pd2ArcherCycle) return pd2ArcherAction(id,target,tick,controller);
    if(unit.rule.ai.kind==MonsterAiKind::ArcaneTower)
        if(const auto result=pd2RathmaSpire(id,tick,controller)) return result;
    if(unit.rule.ai.kind==MonsterAiKind::DoomKnight && unit.rule.nativeClass==1060)
        return pd2Warlord(id,target,point,size,tick,controller);
    if(unit.rule.ai.kind==MonsterAiKind::Willowisp && unit.rule.ai.pd2Parameters[6][2])
        return pd2Willowisp(id,target,tick,controller);
    if(unit.rule.ai.kind==MonsterAiKind::Fallen && unit.rule.ai.pd2Parameters[7][2]==1)
        return pd2TreasureFallen(id,target,point,distance,tick,controller);
    const auto kind=unit.rule.ai.kind;
    const bool brute=kind==MonsterAiKind::Brute && unit.rule.nativeClass==1103;
    const bool mummy=kind==MonsterAiKind::GreaterMummy && unit.rule.nativeClass==1104 && target;
    const bool demon=kind==MonsterAiKind::Megademon && unit.rule.ai.pd2Parameters[6][2];
    const bool priest=kind==MonsterAiKind::ZakarumPriest && unit.rule.ai.pd2Parameters[6][2];
    const bool highPriest=kind==MonsterAiKind::HighPriest && unit.rule.ai.pd2Parameters[6][2];
    const bool siege=kind==MonsterAiKind::SiegeBeast && unit.rule.nativeClass==1000 && target;
    const bool scimitar=kind==MonsterAiKind::FlyingScimitar && unit.rule.ai.pd2Parameters[6][2];
    const bool sharptooth=kind==MonsterAiKind::Overseer && unit.rule.nativeClass==1105 && target;
    const bool summoner=kind==MonsterAiKind::Summoner && (unit.rule.nativeClass==747 || unit.rule.nativeClass==1093);
    const bool westmarch=kind==MonsterAiKind::BaalMinion && unit.rule.nativeClass==996;
    const bool baalBuff=kind==MonsterAiKind::BaalMinion && unit.rule.nativeClass!=938 && unit.rule.ai.pd2Parameters[4][2];
    if(!brute && !mummy && !demon && !priest && !highPriest && !siege && !scimitar && !sharptooth && !summoner && !westmarch && !baalBuff) return {};
    const auto cast=[&](size_t slot,UnitTarget victim,std::optional<Vec> at={},std::optional<int> wait={},bool waitAfter=false) {
        const auto skill=unit.rule.skillIds.at(slot);
        const auto action=unit.rule.skillActions.find(skill);
        if(!skill) return usePd2Skill(controller,{id,UINT16_MAX,victim,tick.tick,1,at},wait,waitAfter);
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,victim,tick.tick,action->second.nativeMode,at},wait,waitAfter);
    };
    const auto primary=target.value_or(UnitTarget{});
    const auto values=ports_.effects.unitStatValues(id,tick.tick);
    if(!values) return StepStatus::Blocked;
    const auto stat=[&](std::string_view name){return nativeStatValue(*values,*unit.rule.stats,name);};
    const auto written=[&](std::vector<std::pair<std::string,int32_t>> writes) {
        const auto result=setPd2AiStats(controller,tick,std::move(writes));
        return result || isDeferred(result.status)?std::optional<StepStatus>{}:std::optional{StepStatus::NotImplemented};
    };
    if(highPriest) {
        // Current 357640 wraps Fn052 for every nonzero fixed aip7(H).
        // An absent fourth skill still consumes native UseSkill failure
        // before the persistent cooldown switches to the close-range path.
        const int cooldown=stat("mon_cooldown1");
        if(cooldown<1) {
            if(playerLifePercentage(unit.life,unit.maximumLife)>unit.rule.ai.pd2Parameters[6][2]) return {};
            const auto result=cast(3,primary);
            if(const auto failed=written({{"mon_cooldown1",1}})) return failed;
            return result;
        }
        if(distance<3) return cast(0,primary);
        controller.nextDecision=tick.tick;
        const auto moved=walkToTargetWithFallback(controller,primary,point,tick);
        return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    if(westmarch) {
        const auto &area=ports_.areas.at(unit.area);
        const auto move=[&] {
            const auto result=perform(controller,[this,id,point,primary,region=unit.area,generation=area.generation] {
                return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
            });
            controller.pursuing=bool(result);controller.nextDecision=tick.tick;
            return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        };
        if(playerLifePercentage(unit.life,unit.maximumLife)<76) {
            const int phase=stat("mon_cooldown1");
            if(phase<1) {
                if(const auto failed=written({{"mon_cooldown1",1}})) return failed;
                return cast(2,primary);
            }
            if(phase<10) {
                if(const auto failed=written({{"mon_cooldown1",phase+1}})) return failed;
                if(!target || !unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) return move();
                if(const auto failed=written({{"mon_cooldown1",10}})) return failed;
                return cast(3,primary,{},unit.rule.ai.pd2FixedIdle,true);
            }
        }
        if(distance>34) return move();
        const int roll=int(limitedRandom(controller.random,100));
        const int first=unit.rule.ai.pd2Parameters[5][2],second=unit.rule.ai.pd2Parameters[6][2];
        if(roll<=first) return cast(0,primary);
        if(roll<=first+second) return cast(1,primary);
        return move();
    }
    if(baalBuff) {
        // 357EE0's fixed aip5(H) gate is separate from the ordinary Fn141
        // probabilities. Stat 69 is a real base write, not an AI speed cache.
        if(const auto failed=written({{"other_animrate",0}})) return failed;
        if(playerLifePercentage(unit.life,unit.maximumLife)<76) {
            const int cooldown=stat("mon_cooldown1");
            if(cooldown<1) {
                if(const auto failed=written({{"other_animrate",-200}})) return failed;
                const auto result=cast(3,primary,{},unit.rule.ai.pd2FixedIdle*4);
                if(const auto failed=written({{"mon_cooldown1",int16_t(unit.rule.ai.pd2Parameters[7][2])}})) return failed;
                return result;
            }
            if(const auto failed=written({{"mon_cooldown1",cooldown-1}})) return failed;
        }
        const auto &area=ports_.areas.at(unit.area);
        if(!target || !unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) {
            const auto moved=perform(controller,[this,id,point,primary,region=unit.area,generation=area.generation] {
                return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
            });
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick;
            return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
        const auto slot=int(limitedRandom(controller.random,100))<=unit.rule.ai.pd2Parameters[5][2]?1u:0u;
        return cast(slot,primary,{},unit.rule.ai.pd2FixedIdle,true);
    }
    if(summoner) {
        if(unit.rule.nativeClass==1093) {
            // 358350 draws CRT even when its extra meteor has no target.
            const int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
            if(roll>=10 || !target) return {};
            const auto &origin=ports_.areas.at(unit.area).definition.origin;
            const int x=int(std::floor(point.x+origin.x))-8+int(pd2CrtRandom(ports_.pd2Random)%16);
            const int y=int(std::floor(point.y+origin.y))-8+int(pd2CrtRandom(ports_.pd2Random)%16);
            return cast(7,{},Vec{float(x)-origin.x,float(y)-origin.y});
        }
        if(!target) return {};
        if(distance>35) {
            const auto &area=ports_.areas.at(unit.area);
            const auto moved=perform(controller,[this,id,point,primary,region=unit.area,generation=area.generation] {
                return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
            });
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick;
            return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        if(stat("mon_cooldown3")) {
            const auto result=cast(5,primary);
            if(const auto failed=written({{"mon_cooldown3",0}})) return failed;
            return result;
        }
        if(int(limitedRandom(controller.random,100))<=unit.rule.ai.pd2Parameters[1][0]) {
            if(const auto failed=written({{"mon_cooldown3",1}})) return failed;
            const auto result=perform(controller,[this,id] {
                ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
            });
            controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(unit.rule.ai.pd2FixedIdle);
            return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        return {}; // The same remaining seed goes to ordinary Fn053.
    }
    if(sharptooth) {
        // 35DA60's ordinary-map prefix owns Armageddon and two poison slots;
        // Invader/Lucion controller branches are separate scene programs.
        const auto row=unit.rule.formulas->skills.find(unit.rule.skillIds[0]);
        if(row==unit.rule.formulas->skills.end()) return StepStatus::NotImplemented;
        if(!ports_.effects.unitStates(id,tick.tick).contains(row->second.auraState)) return cast(0,primary);
        const auto roll=pd2UnitEventRandom(controller.random)%100;
        if(roll<30 || (roll<60 && distance>=16)) return cast(1,primary);
        if(roll<70) return cast(2,primary);
        controller.nextDecision=tick.tick+5;return StepStatus::Complete;
    }
    if(brute) {
        // Core 357730: the same CRT draw drives both attacks. Otherwise the
        // wrapper delegates Fn007; skill identities/modes remain MPQ inputs.
        const auto first=unit.rule.skillIds[0];
        const auto row=unit.rule.formulas->skills.find(first);
        if(row==unit.rule.formulas->skills.end()) return StepStatus::NotImplemented;
        if(!ports_.effects.unitStates(id,tick.tick).contains(row->second.auraState)) return cast(0,primary);
        const int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
        if(roll<20) return cast(distance<20?1:2,primary);
        return {};
    }
    if(mummy) {
        // Core 35DFE0: exactly one unit draw; only teleport takes two CRT draws.
        const auto roll=pd2UnitEventRandom(controller.random)%100;
        if(roll<30) return cast(0,primary);
        if(roll<60) return cast(2,primary);
        if(roll<90) return cast(3,primary);
        const auto &origin=ports_.areas.at(unit.area).definition.origin;
        const int x=int(std::floor(unit.position.x+origin.x))+int(pd2CrtRandom(ports_.pd2Random)%40)-20;
        const int y=int(std::floor(unit.position.y+origin.y))+int(pd2CrtRandom(ports_.pd2Random)%40)-20;
        return cast(1,{},Vec{float(x)-origin.x,float(y)-origin.y});
    }
    const int cooldown=stat("mon_cooldown1");
    if(scimitar) {
        // 356FC0 alternates real timed Do18 buffs. These are not selected
        // paladin auras: each cast keeps its own MPQ duration and state.
        if(cooldown<1) {
            const int alternate=stat("mon_cooldown2");
            const auto result=cast(alternate?3:2,primary);
            if(const auto failed=written({{"mon_cooldown2",alternate?0:1},
                    {"mon_cooldown1",unit.rule.ai.pd2Parameters[5][2]}})) return failed;
            return result;
        }
        if(const auto failed=written({{"mon_cooldown1",cooldown-1}})) return failed;
        if(stat("mon_cooldown3")) {
            if(const auto failed=written({{"mon_cooldown3",0}})) return failed;
            const auto result=perform(controller,[this,id] {
                ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
            });
            controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(unit.rule.ai.pd2FixedIdle);
            return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        const auto &area=ports_.areas.at(unit.area);
        if(target && unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) {
            const auto result=cast(0,primary);
            if(const auto failed=written({{"mon_cooldown3",1}})) return failed;
            return result;
        }
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
        if(int(limitedRandom(controller.random,100))<=unit.rule.ai.pd2Parameters[4][2]) return cast(1,primary);
        const auto moved=perform(controller,[this,id,point,primary,region=unit.area,generation=area.generation] {
            return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
        });
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick;
        return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    if(siege) {
        // Core 35E120 shares one unit draw across cooldown reset, buff,
        // close nova and orb selection. No CRT draw or private counter.
        const int roll=int(pd2UnitEventRandom(controller.random)%100);
        const int second=stat("mon_cooldown2");
        if(cooldown<1 && roll<50) {
            if(const auto failed=written({{"mon_cooldown1",roll%3+4},{"mon_cooldown2",(roll%3*5+15)*2}})) return failed;
            return cast(1,primary);
        }
        if(cooldown>0) if(const auto failed=written({{"mon_cooldown1",cooldown-1}})) return failed;
        if(second>0) {
            if(roll<second) {
                if(const auto failed=written({{"mon_cooldown2",0}})) return failed;
                return cast(3,primary);
            }
            if(const auto failed=written({{"mon_cooldown2",second<15?0:second-10}})) return failed;
        }
        if(roll<30 && distance<15) return cast(2,primary);
        const auto &area=ports_.areas.at(unit.area);
        if(roll<=59 && unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) {
            const auto result=requestAction(controller,{id,0,primary,tick.tick,4});
            controller.nextDecision=tick.tick;
            return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        if(roll<50) return cast(0,primary);
        const auto moved=perform(controller,[this,id,point,primary,region=unit.area,generation=area.generation] {
            return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
        });
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+uint64_t(unit.rule.ai.pd2FixedIdle);
        return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    if(demon) {
        // Core 357550 reads H parameters even in normal/nightmare, then
        // delegates Fn089 once neither explicit cooldown action was selected.
        if(cooldown<1) {
            const auto result=cast(1,primary);
            if(const auto failed=written({{"mon_cooldown1",unit.rule.ai.pd2Parameters[6][2]}})) return failed;
            return result;
        }
        if(const auto failed=written({{"mon_cooldown1",cooldown-1}})) return failed;
        const int second=stat("mon_cooldown2");
        if(second<1) {
            const auto result=cast(2,primary);
            if(const auto failed=written({{"mon_cooldown2",unit.rule.ai.pd2Parameters[7][2]}})) return failed;
            return result;
        }
        if(const auto failed=written({{"mon_cooldown2",second-1}})) return failed;
        // The writes published this loan before the delegate makes its own
        // chance rolls. Start a fresh loan from the live seed for Fn089.
        controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
        controller.randomPublished=false;
        return {};
    }
    // Core 357640 switches permanently into its spawned phase at the H gate.
    if(cooldown<1) {
        if(playerLifePercentage(unit.life,unit.maximumLife)>unit.rule.ai.pd2Parameters[6][2]) return {};
        const auto result=cast(7,primary);
        if(const auto failed=written({{"mon_cooldown1",1}})) return failed;
        return result;
    }
    if(distance<3) return cast(4,primary);
    controller.nextDecision=tick.tick;
    const auto moved=walkToTargetWithFallback(controller,primary,point,tick);
    return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
