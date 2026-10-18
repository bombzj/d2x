#include "system.hpp"
#include "server/systems/actions/release.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "evaluation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace d2x::server::skills {
DomainResult<> System::releaseMonsterSpell(MonsterRelease &pending,TickContext tick) {
    const auto *source=ports_.monsters.find(pending.request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
    const auto formulas=source->rule.formulas;const auto &catalog=*formulas;const auto &program=catalog.skills.at(pending.request.skill);
    const int function=program.number("srvdofunc");
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(function==100) {
        const auto region=source->area;
        if(!pending.directSkill) {
            const auto target=std::get<UnitTarget>(pending.request.target).id;
            if(!ports_.monsters.nativeTargetPosition(target,source->area)) return done();
            pending.directSkill=std::make_shared<effects::NativeAreaPulse>();auto &spec=pending.directSkill->spec;
            spec.source=source->id;spec.area=source->area;spec.skill=pending.request.skill;spec.rank=pending.attack.rank;
            spec.targets=std::vector<EntityId>{target};spec.directFunction=function;
            spec.resultFlags=uint16_t(program.number("resultflags"))|1;spec.hitFlags=uint32_t(program.number("hitflags"));
        }
        pending.nativeCallbackStarted=true;
        const auto result=ports_.effects.nativeArea(pending.directSkill->spec,tick.tick,pending.directSkill);
        if(!result) return result;
        if(program.serverOverlay>0) {
            const auto target=std::get<UnitTarget>(pending.request.target);
            const auto recipient=ports_.monsters.find(target.id);const auto player=ports_.players.findActor(target.id);
            const auto area=recipient?recipient->area:player?player->area:region;
            const auto visual=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},area},
                {OverlayFact{target.id,target.type,area,program.serverOverlay}}});
            if(!visual) return {visual.status,{}};
        }
        pending.nativeCallbackStarted=false;return done();
    }
    const auto missile=ports_.missiles.nativeDefinition(program.reference("srvmissilea"));
    if(!missile || (function!=17 && function!=28 && function!=99 && function!=102)) return {DomainStatus::Unavailable,{}};
    const auto &row=catalog.missiles.at(*missile);
    if(function==28) {
        const auto victim=std::get<UnitTarget>(pending.request.target);
        const auto target=ports_.monsters.nativeTargetPosition(victim.id,source->area);
        const auto point=target?std::optional{target->first}:pending.launch?std::optional{pending.launch->second}:std::nullopt;
        if(!point) return done();
        const auto &area=ports_.areas.at(source->area);
        const auto global=*point+area.definition.origin;
        const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
        const auto room=ports_.areas.roomAtPosition(source->area,source->position,x,y);
        if(!x || !y || !room || monsterAiDistance(source->position,0,*point)>100 ||
           ports_.areas.nativeCollisionMask(*room,x,y,row.number("size"),5,ports_.spatial.nativeCollisionSample(5))) return done();
        missiles::NativeSpawn birth{source->id,source->area,pending.generation,tick.tick,*point,*point,*missile,pending.request.skill,pending.attack.rank};
        const auto born=ports_.missiles.spawnNative(birth);return {born.status,born?std::optional{std::monostate{}}:std::nullopt};
    }
    if(!pending.formulaPrepared) {
        const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        auto input=formulaInput(*source,*sampled);SkillFormulaValues values(catalog,input);
        const auto count=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"calc1");
        const auto step=function==99?values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"calc2"):RuleNumber{0};
        if(!count || !step) return {DomainStatus::Unavailable,{}};
        pending.formulaValue=*count;pending.formulaValue2=std::max(*step,1);pending.formulaPrepared=true;
    }
    std::vector<missiles::NativeSpawn> births;
    if(function==17 || function==102) {
        const auto target=ports_.monsters.nativeTargetPosition(std::get<UnitTarget>(pending.request.target).id,source->area);
        if(!target) return done();
        const auto global=target->first+ports_.areas.at(source->area).definition.origin;
        if(!int(global.x) || !int(global.y) || pending.formulaValue<=0) return done();
        if(pending.formulaValue>512) return {DomainStatus::Capacity,{}};
        for(int index=0;index<pending.formulaValue;++index) {
            missiles::NativeSpawn birth{source->id,source->area,pending.generation,tick.tick,source->position,target->first,*missile,function==102?0:pending.request.skill,pending.attack.rank};
            birth.initialization=function==102?missiles::NativeSpawn::Initialization::DiabWall:missiles::NativeSpawn::Initialization::ChargedSkill;
            birth.initializationIndex=index;births.push_back(birth);
        }
    } else {
        const auto add=[&](int index,int parameter) {
            // Do099 leaves nSkill zero and sets flag0x10: these speeds are
            // already 8.8. The cloud's own MPQ damage uses its supplied rank.
            missiles::NativeSpawn birth{source->id,source->area,pending.generation,tick.tick,source->position,source->position+primePoisonNovaDirection(index),*missile,0,pending.attack.rank};
            birth.nativeVelocity=nativeShiftLeft(row.number(parameter==1?"param1":"param2"),6);birth.velocityFixed=true;
            if(row.number("subloop")) birth.totalFrames=nativeAdd(nativeAdd(row.number("range"),nativeMultiply(row.number("levrange"),pending.attack.rank)),
                nativeMultiply(pending.formulaValue,nativeSubtract(row.number("substop"),row.number("substart"))));
            births.push_back(birth);
        };
        for(int index=0;index<16;index+=2) add(index,1);
        if(pending.formulaValue2>1) for(int index=0;index<15;index+=pending.formulaValue2) add(index+1,2);
    }
    const auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
    const auto born=ports_.missiles.commitNativeBirth(*prepared.value);return {born.status,born?std::optional{std::monostate{}}:std::nullopt};
}
}
