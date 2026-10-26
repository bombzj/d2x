#include "../system.hpp"
#include "../runtime.hpp"
#include "../native_caster.hpp"
#include "execution.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2BossSummon(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->monster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &map=ports_.maps.read();const auto &unit=*caster->monster;const auto &area=ports_.areas.at(unit.area).definition;
    const int function=pending.skill.pd2->doFunction;
    if(!map.instance || (function==179 && map.instance->controller!=43)) return DomainStatus::Unavailable;
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    auto &prepared=pending.pd2Execution->bossSummon;
    if(!prepared) {
        Pd2CastExecution::BossSummon program;
        const auto &row=caster->programs()->formulas->skills.at(pending.skill.sourceId);
        const int life=playerLifePercentage(unit.life,unit.maximumLife);
        if(function==177) {
            const auto definition=pending.skill.pd2->summonClass;
            if(definition<0) return DomainStatus::Unavailable;
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues formulas(*caster->programs()->formulas,input);
            for(int index=0;index<3;++index) {
                const auto count=formulas.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc"+std::to_string(index+1));
                if(!count) throw std::logic_error("Missing native Dclone summon count");
                const bool enabled=life<row.number("param"+std::to_string(index+4));
                std::vector<int> definitions;
                if(index==2) for(int item=0;item<4;++item) definitions.push_back(definition+2+item);
                else definitions.push_back(definition+index);
                program.groups.push_back({std::move(definitions),enabled?*count:0,Vec{0x3b19,0x1994}-area.origin,50});
            }
        } else {
            const auto values=ports_.effects.unitStatValues(actor.actor,actor.tick);if(!values) return DomainStatus::Unavailable;
            const int phase=nativeStatValue(*values,*unit.rule.stats,"mon_cooldown2");
            if(phase==30) program.groups.push_back({{937},life<31?10:life<51?8:life<71?6:4,unit.position,40});
            else if(phase==32) program.groups.push_back({{939},1,unit.position,40,20});
            else if(phase==31) {
                const auto center=(map.phase<3?Vec{0x476c,0x35bf}:map.phase<5?Vec{0x47d3,0x394b}:Vec{0x4e9a,0x1403})-area.origin;
                program.groups={{{938},1,center+Vec{-25,0},0,1,true},{{938},1,center+Vec{0,-25},0,1,false},
                    {{938},1,center+Vec{23,0},0,1,false},{{938},1,center+Vec{0,25},0,1,false}};
            } else if(phase==33) {
                std::vector<int> definitions{1106,969,968};
                if(life<31) definitions.insert(definitions.end(),{1107,969,1106});
                else if(life<51) definitions.insert(definitions.end(),{1107,968});
                else if(life<71) definitions.push_back(969);
                for(const auto definition:definitions) program.groups.push_back({{definition},1,unit.position,40,40,true,15});
                program.sharedFailureBudget=true;
            }
        }
        prepared=std::move(program);
    }
    auto &program=*prepared;pending.nativeCallbackStarted=true;
    while(program.group<program.groups.size()) {
        const auto &group=program.groups[program.group];
        if(program.count>=group.count || program.failures>=group.failureLimit) {
            ++program.group;program.count=0;if(!program.sharedFailureBudget) program.failures=0;
            program.positionPrepared=false;program.point.reset();continue;
        }
        if(!program.positionPrepared) {
            program.definition=group.definitions.front();
            if(group.definitions.size()>1) program.definition=group.definitions[pd2CrtRandom(ports_.pd2Random)%group.definitions.size()];
            auto point=group.center;
            if(group.spread) {
                const int x=int(pd2CrtRandom(ports_.pd2Random)%uint32_t(group.spread))-group.spread/2;
                const int y=int(pd2CrtRandom(ports_.pd2Random)%uint32_t(group.spread))-group.spread/2;
                point=point+Vec{float(x),float(y)};
            }
            program.point=ports_.maps.bossSpawnPosition(actor.area,point,group.search);program.positionPrepared=true;
        }
        bool created=false;
        if(program.point) {
            const auto key="pd2-boss-skill."+std::to_string(actor.actor.value)+"."+std::to_string(pending.tick)+"."+
                std::to_string(program.group)+"."+std::to_string(program.count)+"."+std::to_string(program.failures);
            const auto born=ports_.maps.bossSpawn(*program.point,program.definition,key,{actor.tick});
            if(isDeferred(born.status) || born.status==DomainStatus::NotImplemented) return born.status;
            created=bool(born);
            if(created && group.aiDelay) ports_.monsters.scheduleAi(*born.value,actor.tick+group.aiDelay);
        }
        if(created) ++program.count;else ++program.failures;
        program.positionPrepared=false;program.point.reset();
    }
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
}
