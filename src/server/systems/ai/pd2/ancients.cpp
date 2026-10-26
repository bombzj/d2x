#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::ai {
StepStatus System::pd2Ancients(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &map=ports_.maps.read();
    if(!map.instance || map.instance->controller!=44) return StepStatus::NotImplemented;
    const auto &ai=unit.rule.ai;
    const auto parameter=[&](int index,int column){return ai.pd2Parameters[size_t(index-1)][size_t(column)];};
    const int delay=ai.pd2Delays[1]; // 35CD30 deliberately uses aidel(N).
    const auto idle=[&](int frames) {
        ports_.monsters.neutralMode(id);controller.pursuing=false;controller.pd2MovingTimer=false;
        controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));
    };
    const auto finish=[&] {controller.phase=1;controller.loop=0;};
    const auto status=[](const DomainResult<> &value) {return isDeferred(value.status)?StepStatus::Blocked:value?StepStatus::Complete:StepStatus::NotImplemented;};
    auto acquired=acquireTarget(unit,tick,false,false);
    int nearest=INT32_MAX;std::optional<UnitTarget> playerTarget;Vec playerPoint;
    for(const auto &[key,player]:ports_.players.all()) {
        (void)key;
        if(!player.entered || player.area!=unit.area || player.persistent.player.hp<=0) continue;
        const int x=nativeSubtract(int(player.position.x),int(unit.position.x)),y=nativeSubtract(int(player.position.y),int(unit.position.y));
        const int squared=nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
        if(squared<nearest) {nearest=squared;playerTarget=UnitTarget{player.actor,0,0};playerPoint=player.position;}
    }
    if(!acquired && playerTarget) acquired=NativeAiTarget{*playerTarget,playerPoint,2,0};
    if(!acquired || ports_.effects.unitStates(id,tick.tick).contains(54)) {idle(delay);return StepStatus::Complete;}
    const auto target=acquired->unit;const auto point=acquired->position;
    const bool combat=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,acquired->size,unit.rule.meleeRange);
    const auto cast=[&](size_t slot,std::optional<Vec> at={},bool coordinate=false) {
        const auto skill=unit.rule.skillIds.at(slot);const auto action=unit.rule.skillActions.find(skill);
        if(!skill) return usePd2Skill(controller,{id,UINT16_MAX,{},tick.tick,1,at});
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,coordinate?UnitTarget{}:target,tick.tick,action->second.nativeMode,at});
    };
    const auto move=[&](bool deleteOnFailure,std::optional<int> wait={}) {
        controller.pd2MovingTimer=wait.has_value();
        return status(perform(controller,[this,id,target,point,tick,region=unit.area,generation=area.generation,deleteOnFailure,wait]()->DomainResult<> {
            const auto moved=ports_.monsters.requestMove({id,{region,generation,point},target.id,0,75,false,1});
            if(isDeferred(moved.status)) return moved;
            auto &control=state_.controllers.at(id);control.pursuing=bool(moved);
            control.pd2CancelAi=!moved && deleteOnFailure && !wait;
            control.nextDecision=tick.tick+uint64_t(wait.value_or(1));
            return {DomainStatus::Applied,std::monostate{}};
        }));
    };
    const auto select=[&](int phase,int loop=0) {idle(delay);controller.phase=phase;controller.loop=loop;return StepStatus::Complete;};
    const int definition=unit.rule.nativeClass,phase=controller.phase;
    if(definition==990) {
        switch(phase) {
        case 0:return select(12);
        case 4: {const auto result=move(false,10);finish();return result;}
        case 5: {
            const auto result=perform(controller,[this,id,target,tick] {
                const auto *source=ports_.monsters.find(id);const auto *victim=ports_.monsters.find(target.id);
                const auto *player=ports_.players.findActor(target.id);
                if(!source || (!victim && !player)) return DomainResult<>{DomainStatus::InvalidActor,{}};
                const auto point=player?player->position:victim->position;
                const auto destination=monsterRetreatPoint(source->position,point,30);
                const auto moved=ports_.monsters.requestMove({id,{source->area,ports_.areas.at(source->area).generation,destination},{},0,75,false,1,0,0,13,30});
                if(isDeferred(moved.status)) return moved;
                auto &control=state_.controllers.at(id);control.pursuing=bool(moved);control.nextDecision=tick.tick+20;
                return DomainResult<>{DomainStatus::Applied,std::monostate{}};
            });
            controller.pd2MovingTimer=true;finish();return status(result);
        }
        case 10: {const auto result=cast(0);finish();return result;}
        case 11: {const auto result=cast(1);finish();return result;}
        case 12: {
            const auto boosted=perform(controller,[this,id,tick,percent=ai.pd2SkillRanks[2],region=unit.area]()->DomainResult<> {
                std::vector<monsters::LifeChange> changes;
                for(const auto &[otherId,other]:ports_.monsters.read().actors) {
                    if(other.area!=region || other.nativeMode==12 || !other.nativeStats || !other.rule.stats) continue;
                    const auto sources=ports_.effects.unitStatSources(otherId,tick.tick);
                    attributes::MonsterResourceTrace resources(other,tick.tick,ports_.effects.unitEffects(otherId));resources.sampleSources(sources);
                    const auto maximum=uint32_t(resources.read().maximum.raw());
                    resources.writeBaseStat("maxhp",nativeNarrow(maximum+(maximum/100)*percent),sources);
                    auto change=resources.take();if(!ports_.monsters.canCommitLife(change)) return {DomainStatus::Stale,{}};
                    changes.push_back(std::move(change));
                }
                for(auto &change:changes) if(!ports_.monsters.commitLife(std::move(change)))
                    throw std::logic_error("Preflighted Ancient maximum-life update rejected");
                return {DomainStatus::Applied,std::monostate{}};
            });
            if(!boosted) return status(boosted);
            const auto result=cast(2);finish();return result;
        }
        case 13: {const auto result=cast(3);finish();return result;}
        case 14: {const auto result=cast(4);finish();return result;}
        default: {
            if(nearest>nativeMultiply(parameter(2,0),25)) {idle(2);controller.phase=4;controller.loop=0;return StepStatus::Complete;}
            int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
            const int first=parameter(3,0),second=parameter(3,1),third=parameter(3,2);
            if(roll<first && combat) {idle(2);controller.phase=5;controller.loop=0;return StepStatus::Complete;}
            if(roll<first+second) return select(10);
            if(roll<first+second+third) return select(14);
            roll=int(pd2CrtRandom(ports_.pd2Random)%100);
            if(roll<parameter(4,0)) return select(13);
            if(roll<parameter(4,0)+parameter(4,1)) return select(11);
            return select(roll<parameter(4,0)+parameter(4,1)+parameter(4,2)?12:1);
        }
        }
    }
    if(definition==991) {
        if(nearest>parameter(1,0) && phase!=10 && phase!=14) {
            controller.thirdParameter=0;const auto result=cast(0,point,true);finish();return result;
        }
        switch(phase) {
        case 0:return select(12);
        case 4: {const auto result=move(false,10);finish();return result;}
        case 10: {const auto result=cast(0,point,true);finish();return result;}
        case 11:
        case 13:
            if(!combat) {
                const auto result=move(true,10);const int limit=phase==11?3:2;
                const int count=controller.loop+1;controller.phase=count>limit?1:phase;controller.loop=count>limit?0:count;
                return result;
            } else {const auto result=cast(phase==11?1:3);finish();return result;}
        case 12: {const auto result=cast(2);finish();return result;}
        case 14: {const auto result=cast(4);finish();return result;}
        case 20: {const auto result=cast(0,point,true);controller.phase=13;controller.loop=0;return result;}
        case 21: {const auto result=cast(0,point,true);controller.phase=14;controller.loop=0;return result;}
        default: {
            if(nearest>nativeMultiply(parameter(2,0),25)) {idle(2);controller.phase=4;controller.loop=0;return StepStatus::Complete;}
            const int x=int(point.x)-int(unit.position.x),y=int(point.y)-int(unit.position.y);
            const int distance=nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
            const int roll=int(pd2CrtRandom(ports_.pd2Random)%100),first=parameter(3,0),second=parameter(3,1),third=parameter(3,2);
            if(roll<first || (distance>200 && roll-parameter(2,1)<first)) return select(10);
            if(roll<first+second || (distance<201 && roll-parameter(2,1)<first+second)) return select(11);
            if(roll<first+second+third) return select(13);
            const int extra=int(pd2CrtRandom(ports_.pd2Random)%100);
            if(extra<parameter(4,0)) return select(20);
            if(extra<parameter(4,0)+parameter(4,1)) return select(21);
            return select(extra<parameter(4,0)+parameter(4,1)+parameter(4,2)?12:10);
        }
        }
    }
    if(definition!=989) return StepStatus::NotImplemented;
    if(nearest>parameter(1,0) && phase!=21) {controller.thirdParameter=0;controller.loop=0;}
    else switch(phase) {
    case 0:return select(12);
    case 3: {
        const int radius=5+int(pd2CrtRandom(ports_.pd2Random)%5);
        const auto point=monsterWanderPoint(unit.position,radius,controller.random);
        const auto result=perform(controller,[this,id,point,region=unit.area,generation=area.generation] {
            const auto moved=ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
            return isDeferred(moved.status)?moved:DomainResult<>{DomainStatus::Applied,std::monostate{}};
        });finish();return status(result);
    }
    case 4: {const auto result=move(true);finish();return result;}
    case 10: {const auto result=cast(0);finish();return result;}
    case 11:
        if(!combat) {const auto result=move(true);const int count=controller.loop+1;controller.phase=count>3?1:11;controller.loop=count>3?0:count;return result;}
        else {const auto result=cast(1);finish();return result;}
    case 12: {const auto result=cast(2);finish();return result;}
    case 20: {
        const int count=controller.loop?controller.loop:3;
        if(count%3==0 && count>0) {
            auto x=uint32_t(nativeNarrow(int64_t(std::round(double(int(point.x)-int(unit.position.x))*1.4))));
            auto y=uint32_t(nativeNarrow(int64_t(std::round(double(int(point.y)-int(unit.position.y))*1.4))));
            if(x<5) x=5;
            if(y<5) y=5;
            if(x+4<4) x=uint32_t(-5);
            if(y+4<4) y=uint32_t(-5);
            controller.thirdParameter=nativeNarrow((x<<16)+y);
            const auto result=cast(3,unit.position+Vec{float(nativeNarrow(x)),float(nativeNarrow(y))},true);
            if(count>1) {controller.phase=20;controller.loop=count-1;} else finish();return result;
        }
        if(count%3==2) {
            const int frames=std::max(2,int(std::round((1.-double(nearest)/std::max(100,parameter(2,1)))*delay)));
            idle(frames);controller.phase=20;controller.loop=count-1;return StepStatus::Complete;
        }
        if(count%3==1 && controller.thirdParameter) {
            const auto packed=uint32_t(controller.thirdParameter);
            const auto result=cast(0,unit.position+Vec{float(controller.thirdParameter>>16),float(packed&0xffff)},true);
            controller.phase=count<2?1:20;controller.loop=count<2?0:count-1;return result;
        }
        if(count%3!=1) {finish();idle(delay);return StepStatus::Complete;}
        return select(count<2?1:20,count<2?0:count-1);
    }
    case 21:
        if(controller.loop==1) {
            if(controller.thirdParameter) setPd2AiStats(controller,tick,{{"velocitypercent",controller.thirdParameter}});
            idle(3);finish();return StepStatus::Complete;
        }
        if(controller.loop!=0) {idle(delay);finish();return StepStatus::Complete;}
        break;
    case 22:
        if(!combat) {const auto result=move(true);const int count=controller.loop+1;controller.phase=count>3?1:22;controller.loop=count>3?0:count;return result;}
        else {const auto result=cast(2);finish();return result;}
    default: {
        const int roll=int(pd2CrtRandom(ports_.pd2Random)%100),first=parameter(3,0),second=parameter(3,1),third=parameter(3,2);
        if(roll<first) return select(4);
        if(roll<first+second) return select(11);
        if(roll<first+second+third) return select(22);
        const int extra=int(pd2CrtRandom(ports_.pd2Random)%100);
        if(extra<parameter(4,0)) return select(10);
        if(extra<parameter(4,0)+parameter(4,1)) return select(20);
        if(extra<parameter(4,0)+parameter(4,1)+parameter(4,2)) return select(20,9);
        return select(1);
    }
    }
    if(!unit.nativeStats || !unit.nativeStats->sources) return StepStatus::NotImplemented;
    const int before=baseStatValue(*unit.rule.stats,*unit.nativeStats->sources,{67,0});
    controller.thirdParameter=before;
    const auto written=setPd2AiStats(controller,tick,{{"velocitypercent",nativeAdd(before,parameter(2,0))}});
    if(!written) return status(written);
    const auto result=cast(0);controller.phase=21;controller.loop=1;return result;
}
}
