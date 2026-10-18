#include "system.hpp"
#include "server/systems/actions/release.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/effects/corpse_explosion.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "evaluation.hpp"
#include "native_caster.hpp"
#include "core/random.hpp"
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
    if(function==141) {
        if(!pending.directSkill) {
            auto pulse=std::make_shared<effects::NativeAreaPulse>();
            auto radius=nativeAdd(program.number("param5"),nativeMultiply(pending.attack.rank-1,program.number("param6")));
            if(!radius) radius=nativeAdd(program.number("param1"),nativeMultiply(pending.attack.rank-1,program.number("param2")));
            const auto skill=catalog.skillIds.find("baal corpse explode");if(skill==catalog.skillIds.end()) return {DomainStatus::Unavailable,{}};
            pulse->spec.source=source->id;pulse->spec.area=source->area;pulse->spec.skill=skill->second;pulse->spec.rank=pending.attack.rank;
            pulse->spec.center=source->position;pulse->spec.radius=radius;pulse->spec.filter=0x3002;
            pulse->targets=ports_.effects.nativeAuraCandidateRecipients({source->id,source->area,radius,0x3002,false,
                source->position+ports_.areas.at(source->area).definition.origin,effects::NativeAuraTargetMode::TargetRange,tick.tick});
            pulse->prepared=true;pending.directSkill=std::move(pulse);pending.nativeCallbackStarted=true;
        }
        auto &pulse=*pending.directSkill;
        while(pulse.next<pulse.targets.size()) {
            const auto corpse=pulse.targets[pulse.next];
            if(!pending.corpseExplosion) {
                const auto assigned=ports_.monsters.setNativeTarget(pending.request.actor,corpse);if(!assigned) return assigned;
                pending.corpseExplosion=std::make_shared<effects::CorpseExplosionPulse>();
            }
            const auto exploded=ports_.effects.corpseExplosion(pending.request.actor,corpse,pulse.spec.skill,pulse.spec.rank,tick.tick,pending.corpseExplosion);
            if(isDeferred(exploded.status)) return exploded;
            if(exploded.status==DomainStatus::Conflict || exploded.status==DomainStatus::Stale) return {DomainStatus::Capacity,{}};
            if(!exploded && exploded.status!=DomainStatus::Unavailable && exploded.status!=DomainStatus::InvalidActor) return exploded;
            pending.corpseExplosion.reset();++pulse.next;
        }
        pulse.complete=true;pending.nativeCallbackStarted=false;return done();
    }
    if(function==139) {
        const auto definition=ports_.missiles.nativeDefinition(program.reference("srvmissilea"));
        if(!definition) return {DomainStatus::Unavailable,{}};
        const auto victim=std::get<UnitTarget>(pending.request.target).id;
        const auto target=ports_.monsters.nativeTargetPosition(victim,source->area);if(!target) return done();
        const auto &area=ports_.areas.at(source->area).definition;
        const auto origin=source->position+area.origin,destination=target->first+area.origin;
        const auto dx=nativeSubtract(int(std::floor(destination.x)),int(std::floor(origin.x)));
        const auto dy=nativeSubtract(int(std::floor(destination.y)),int(std::floor(origin.y)));
        missiles::NativeSpawn birth{source->id,source->area,pending.generation,tick.tick,
            source->position,target->first,*definition,pending.request.skill,pending.attack.rank};
        birth.copyMonsterAttackBonus=true;
        // Do139 edits the missile returned by CreateMissile, after that
        // wrapper has already made any multishot companions.
        birth.firstFinalParameters=std::array<int32_t,2>{nativeSubtract(0,dy),dx};
        const auto born=ports_.missiles.spawnNative(birth);
        return {born.status,born?std::optional{std::monostate{}}:std::nullopt};
    }
    if(function==140) {
        if(!pending.tentacles) {
            actions::MonsterTentacleExecution execution;
            const auto expected=source->combatRandom;auto random=expected;
            execution.count=source->rule.difficulty+int(rollRandom(random)%3)+2;
            if(source->rule.baalTentacleClasses[0]>=0) {
                const auto variant=source->rule.difficulty+int(limitedRandom(random,2));
                if(variant<0 || variant>=int(source->rule.baalTentacleClasses.size())) return {DomainStatus::Unavailable,{}};
                execution.nativeClass=source->rule.baalTentacleClasses[size_t(variant)];execution.nativeMode=4;
                // GetMinionSpawnInfo rolls these coordinates even though
                // Do140 replaces both with coordinates around the target.
                (void)limitedRandom(random,24);(void)limitedRandom(random,24);
            } else execution.nativeClass=source->rule.spawnFallbackClass;
            if(execution.nativeClass<0) return {DomainStatus::Unavailable,{}};
            const auto victim=std::get<UnitTarget>(pending.request.target).id;
            const auto point=ports_.monsters.nativeTargetPosition(victim,source->area);
            const auto &area=ports_.areas.at(source->area).definition;
            const auto global=point.value_or(std::pair{source->position,0}).first+area.origin;
            execution.center=Vec{float(std::floor(global.x)),float(std::floor(global.y))}-area.origin;
            const auto consumed=ports_.monsters.consumeRandom(source->id,expected,random);if(!consumed) return consumed;
            pending.tentacles=execution;pending.nativeCallbackStarted=true;
        }
        auto &execution=*pending.tentacles;
        while(execution.next<execution.count) {
            source=ports_.monsters.find(pending.request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
            if(!execution.point) {
                const auto expected=source->combatRandom;auto random=expected;
                const auto x=int(rollRandom(random)%18)-9,y=int(rollRandom(random)%18)-9;
                const auto consumed=ports_.monsters.consumeRandom(source->id,expected,random);if(!consumed) return consumed;
                execution.point=execution.center+Vec{float(x),float(y)};
            }
            const auto born=ports_.monsters.spawnMinionChild(source->id,*execution.point,pending.request.skill,
                {execution.nativeClass,execution.nativeMode,0},tick.tick,ports_.spatial.nativeCollisionSample(),MinionBirthProcedure::BaalTentacle);
            if(isDeferred(born.status)) return {born.status,{}};
            if(born.status!=DomainStatus::Applied && born.status!=DomainStatus::Unavailable) return {born.status,{}};
            execution.point.reset();++execution.next;
        }
        pending.nativeCallbackStarted=false;return done();
    }
    if(function==68) {
        if(!pending.formulaPrepared) {
            const auto definition=ports_.missiles.nativeDefinition(program.reference("srvmissilea"));if(!definition) return {DomainStatus::Unavailable,{}};
            std::vector<missiles::NativeSpawn> births;
            for(const auto heading:missileRingBurst(1)) births.push_back({source->id,source->area,pending.generation,tick.tick,
                source->position,source->position+heading,*definition,pending.request.skill,pending.attack.rank});
            auto birth=ports_.missiles.prepareNativeBirth(births);
            if(!birth && isDeferred(birth.status)) return {birth.status,{}};
            if(birth) {const auto published=ports_.missiles.commitNativeBirth(*birth.value);if(!published) return {published.status,{}};}
            pending.formulaPrepared=true;pending.nativeCallbackStarted=true;
        }
        if(!pending.formulaValue2) {
            source=ports_.monsters.find(pending.request.actor);if(!source) return {DomainStatus::InvalidActor,{}};
            const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
            const auto expected=source->combatRandom;auto random=expected;
            const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
                return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
            };
            auto input=formulaInput(*source,*sampled,draw);SkillFormulaValues values(catalog,input);
            const auto duration=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"auralencalc");
            if(!duration) return {DomainStatus::Unavailable,{}};
            const auto consumed=ports_.monsters.consumeRandom(source->id,expected,random);if(!consumed) return consumed;
            pending.formulaValue=*duration;pending.formulaValue2=1;
        }
        const auto result=ports_.effects.skillAuraState(pending.request.actor,pending.request.actor,pending.request.skill,pending.attack.rank,
            pending.formulaValue,tick.tick,effects::NativeSkillStateProcedure::Warcry);
        pending.nativeCallbackStarted=isDeferred(result.status);return result;
    }
    if(function==55) {
        const auto target=std::get<UnitTarget>(pending.request.target);
        const auto result=ports_.effects.corpseExplosion(source->id,target.id,pending.request.skill,pending.attack.rank,tick.tick,pending.corpseExplosion);
        pending.nativeCallbackStarted=pending.corpseExplosion && pending.corpseExplosion->prepared && !pending.corpseExplosion->complete;
        return result;
    }
    if(function==104) {
        const auto victim=std::get<UnitTarget>(pending.request.target);
        RegionId region;Vec point;
        if(victim.type==2) {
            const auto object=ports_.objects.read().objects.find(victim.id);
            auto at=ports_.travel.nativePortalPosition(victim.id);
            if(object!=ports_.objects.read().objects.end()) at=std::pair{object->second.area,object->second.position};
            if(!at) return done();
            region=at->first;point=at->second;
        } else {
            const auto target=ports_.monsters.nativeTargetPosition(victim.id,source->area);if(!target) return done();
            const auto *player=ports_.players.findActor(victim.id);const auto *monster=ports_.monsters.find(victim.id);
            if(!player && !monster) return done();
            region=player?player->area:monster->area;
            point=target->first+ports_.areas.at(source->area).definition.origin-ports_.areas.at(region).definition.origin;
        }
        const auto &area=ports_.areas.at(region);
        if(area.definition.town) return done();
        const auto summon=program.reference("summon");
        const auto first=std::ranges::find_if(area.definition.nativeMonsterClasses,[&](const auto &entry){return entry.second.identity.monster==summon;});
        if(first==area.definition.nativeMonsterClasses.end()) return {DomainStatus::Unavailable,{}};
        return ports_.monsters.spawnDiabloPrison(region,point,first->first,tick.tick,ports_.spatial.nativeCollisionSample());
    }
    if(function==124) {
        const auto caster=nativeCaster(ports_.players,ports_.monsters,source->id);
        if(!caster || !caster->programs() || !caster->programs()->definitions.contains(pending.request.skill)) return {DomainStatus::Unavailable,{}};
        if(!pending.nativeSkill) {
            const auto actor=caster->context(ports_.areas,tick.tick);
            auto skill=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,pending.attack.rank,tick.tick);
            pending.nativeSkill=std::make_shared<Release>(Release{actor,std::move(skill),{},
                {source->area,pending.generation,source->position},{},tick.tick,1});
        }
        const auto result=startDruidPeriodic(*pending.nativeSkill,caster->context(ports_.areas,tick.tick));
        return {result,result==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
    }
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
    if(!missile || (function!=17 && function!=24 && function!=28 && function!=99 && function!=102)) return {DomainStatus::Unavailable,{}};
    const auto &row=catalog.missiles.at(*missile);
    if(function==24) {
        const auto victim=std::get<UnitTarget>(pending.request.target).id;
        const auto target=victim!=source->id?ports_.monsters.nativeTargetPosition(victim,source->area):std::nullopt;
        const auto point=target?std::optional{target->first}:pending.launch?std::optional{pending.launch->second}:std::nullopt;
        if(!point) return done();
        const auto &area=ports_.areas.at(source->area);
        const auto global=*point+area.definition.origin;
        const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
        const auto room=ports_.areas.roomAtPosition(source->area,source->position,x,y);
        if(!x || !y || !room || ports_.areas.at(room->area).definition.town ||
           ports_.areas.nativeCollisionMask(*room,x,y,row.number("size"),5,ports_.spatial.nativeCollisionSample(5))) return done();
        const auto owner=source->position+area.definition.origin;
        const int dx=nativeSubtract(int(std::floor(owner.x)),x),dy=nativeSubtract(int(std::floor(owner.y)),y);
        const Vec at{float(x)-area.definition.origin.x,float(y)-area.definition.origin.y};
        // Retail Do024 (RVA437D0) constructs the two makers before the
        // optional center fire, with raw params rather than CreateMissile's
        // monster attack bonus / multishot wrapper. Their unnormalised
        // perpendicular endpoints use integer world coordinates.
        std::vector<missiles::NativeSpawn> births;
        births.push_back({source->id,source->area,pending.generation,tick.tick,at,
            at+Vec{-float(dy),float(dx)},*missile,pending.request.skill,pending.attack.rank});
        births.push_back({source->id,source->area,pending.generation,tick.tick,at,
            at+Vec{float(dy),-float(dx)},*missile,pending.request.skill,pending.attack.rank});
        if(const auto fire=ports_.missiles.nativeDefinition(program.reference("srvmissileb")))
            births.push_back({source->id,source->area,pending.generation,tick.tick,at,at,*fire,pending.request.skill,pending.attack.rank});
        const auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return {prepared.status,{}};
        const auto born=ports_.missiles.commitNativeBirth(*prepared.value);return {born.status,born?std::optional{std::monostate{}}:std::nullopt};
    }
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
