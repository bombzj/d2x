#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2DiabloClone(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &map=ports_.maps.read();
    if(unit.rule.nativeClass!=789 || !map.instance || map.instance->controller!=42) return {};
    const auto &area=ports_.areas.at(unit.area);const auto &ai=unit.rule.ai;
    const auto parameter=[&](size_t index,size_t column){return ai.pd2Parameters.at(index-1).at(column);};
    if(!parameter(3,2)) return StepStatus::NotImplemented;
    ports_.maps.bind(0,id,unit.area,unit.position,false);
    const int difficulty=map.instance->bossDifficulty;
    auto target=acquireTarget(unit,tick,false,false);
    int nearest=INT32_MAX,farthest=0,participants=0;std::optional<NativeAiTarget> nearestPlayer;
    const auto squared=[](Vec a,Vec b) {const int x=nativeSubtract(int(a.x),int(b.x)),y=nativeSubtract(int(a.y),int(b.y));return nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));};
    for(const auto playerId:ports_.players.nativeActorOrder()) {
        const auto *player=ports_.players.findActor(playerId);
        if(!player || !player->entered || player->area!=unit.area || player->persistent.player.hp<=0) continue;
        ++participants;const int distance=squared(player->position,unit.position);farthest=std::max(farthest,distance);
        if(distance<nearest) {nearest=distance;nearestPlayer=NativeAiTarget{UnitTarget{playerId,0,0},player->position,2,0};}
    }
    const auto status=[](DomainResult<> value) {return isDeferred(value.status)?StepStatus::Blocked:value?StepStatus::Complete:StepStatus::NotImplemented;};
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));return StepStatus::Complete;};
    const auto guard=[&](bool active) {return setPd2Guard(controller,tick,active);};
    if(map.pauseDepth>0) {
        if(nearestPlayer) ports_.maps.consumeScenePause();
        const auto guarded=guard(true);if(!guarded) return status(guarded);return idle(10);
    }
    bool summoned=false;
    for(const auto &[otherId,other]:ports_.monsters.read().actors) if(otherId!=id && other.area==unit.area &&
        other.nativeMode!=12 && other.identity.nativeClass>=886 && other.identity.nativeClass<=892) {summoned=true;break;}
    const int delay=std::max(25,int(ai.pd2Delays[1])-difficulty*8+8);
    if(participants>0) {
        if(participants<controller.summonChance) {
            const auto healed=perform(controller,[this,id,tick] {
                const auto &actor=*ports_.monsters.find(id);const auto sources=ports_.effects.unitStatSources(id,tick.tick);
                attributes::MonsterResourceTrace trace(actor,tick.tick,ports_.effects.unitEffects(id));trace.sampleSources(sources);
                const auto maximum=uint32_t(trace.read().maximum.raw());
                trace.seedLife(Fixed8::fromRaw(nativeNarrow(std::min(maximum,uint32_t(trace.read().life.raw())+(maximum/100)*20))));
                return ports_.monsters.commitLife(trace.take())?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Stale,{}};
            });
            if(!healed) return status(healed);
        }
        controller.summonChance=participants;
    }
    if(!target) target=nearestPlayer;
    if(!target) {const auto guarded=guard(true);if(!guarded) return status(guarded);return idle(delay);}
    const auto cast=[&](size_t slot,std::optional<Vec> at={},std::optional<int> wait={}) {
        const auto skill=unit.rule.skillIds.at(slot);const auto action=unit.rule.skillActions.find(skill);
        if(!skill) return usePd2Skill(controller,{id,UINT16_MAX,{},tick.tick,1,at},wait,true);
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,at?UnitTarget{}:target->unit,tick.tick,action->second.nativeMode,at},wait,true);
    };
    const auto snapshot=ports_.effects.unitStatValues(id,tick.tick);if(!snapshot) return StepStatus::Blocked;
    const auto stat=[&](std::string_view name){return nativeStatValue(*snapshot,*unit.rule.stats,name);};
    const auto write=[&](std::vector<std::pair<std::string,int32_t>> values){return setPd2AiStats(controller,tick,std::move(values));};
    const int stage=stat("mon_cooldown2"),life=playerLifePercentage(unit.life,unit.maximumLife);
    int cooldown=stat("mon_cooldown1");
    if((life<75 && stage==2) || (life<50 && stage==4) || (life<25 && stage==6)) {
        const auto healed=perform(controller,[this,id,tick,stage] {
            const auto &actor=*ports_.monsters.find(id);
            attributes::MonsterResourceTrace trace(actor,tick.tick,ports_.effects.unitEffects(id));trace.sampleSources(ports_.effects.unitStatSources(id,tick.tick));
            trace.seedLife(Fixed8::fromRaw(pd2MultiplyDivide(trace.read().maximum.raw(),100.-double(stage/2)*25.,100)));
            return ports_.monsters.commitLife(trace.take())?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Stale,{}};
        });
        if(!healed) return status(healed);
        const auto written=write({{"mon_cooldown1",parameter(5,0)},{"mon_cooldown2",nativeAdd(stage,1)}});if(!written) return status(written);
        cooldown=parameter(5,0);
    }
    // Read the post-threshold counter, while native selection retains the
    // old stage/life sampled above until the next AI callback.
    const auto center=Vec{0x3b19,0x1994}-area.definition.origin;
    if(cooldown==parameter(5,0)) {
        write({{"mon_cooldown1",nativeSubtract(cooldown,1)}});
    } else if(cooldown<1) {
        write({{"passive_cold_mastery",0},{"item_pierce_cold",0}});
        if(!stage || farthest>380) summoned=true;
        const auto guarded=guard(summoned);if(!guarded) return status(guarded);
        if(controller.randomPublished) {controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;controller.randomPublished=false;}
        if(!stage) {write({{"mon_cooldown1",0},{"mon_cooldown2",1}});return idle(50);}
        const auto summon=unit.rule.formulas->skills.find(unit.rule.skillIds[5]);
        if(summon==unit.rule.formulas->skills.end()) return StepStatus::NotImplemented;
        if((life<summon->second.number("param4") && stage==1) || (life<summon->second.number("param5") && stage==3) ||
           (life<summon->second.number("param6") && stage==5)) {
            write({{"mon_cooldown2",nativeAdd(stage,1)}});controller.phase=0;return cast(5,{},delay);
        }
        const int bias=farthest>380?-20:controller.phase==1?10:0;
        const int run=farthest>380?difficulty*8+25:farthest<16?-15:difficulty*8;
        if(controller.phase!=2 && int(limitedRandom(controller.random,100))<parameter(8,0)+run) {controller.phase=2;return cast(6,{},delay);}
        if(int(limitedRandom(controller.random,100))-difficulty*8<parameter(7,0)) {controller.phase=0;return idle(delay);}
        const int choice=int(limitedRandom(controller.random,100))+bias;
        controller.phase=0;
        if(choice<parameter(1,2)) return cast(0,{},delay);
        if(choice<parameter(1,2)+parameter(2,2)) return cast(2,{},delay);
        if(choice<parameter(1,2)+parameter(2,2)+parameter(3,2) &&
           unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,target->position,target->size,unit.rule.meleeRange)) return cast(1,{},delay);
        controller.phase=1;return cast(3,{},delay+10);
    } else {
        const int distance=squared(unit.position,center);
        if(distance<31) {
            if(distance<3) {
                const int bonus=nativeMultiply(nativeAdd(nativeSubtract(parameter(5,0),cooldown),1),parameter(6,0));
                write({{"passive_cold_mastery",bonus},{"item_pierce_cold",bonus},{"mon_cooldown1",nativeSubtract(cooldown,1)}});
                const auto result=cast(4,{},delay);guard(true);return result;
            }
            for(const auto &[otherId,other]:ports_.monsters.read().actors)
                if(otherId!=id && other.area==unit.area && other.nativeMode!=12 && squared(other.position,center)<11)
                    perform(controller,[this,otherId,tick] {return ports_.monsters.scriptDeath(otherId,tick.tick,{},true);});
            return cast(7,center,50);
        }
    }
    const auto result=cast(6,center,50);guard(true);return result;
}
}
