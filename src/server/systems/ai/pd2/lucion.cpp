#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>
#include <memory>

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2Lucion(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &map=ports_.maps.read();
    const int type=unit.identity.nativeClass;
    if(!map.instance || map.instance->controller!=45 || (type!=1112 && type!=1113 && type!=1141)) return {};
    const auto &area=ports_.areas.at(unit.area);const auto &ai=unit.rule.ai;
    const int difficulty=map.instance->bossDifficulty;
    const auto parameter=[&](size_t index,size_t column){return int(ai.pd2Parameters.at(index-1).at(column));};
    const auto draw=[&] {
        if(controller.randomPublished) {
            controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
            controller.randomPublished=false;
        }
        return pd2UnitEventRandom(controller.random);
    };
    const auto status=[](const DomainResult<> &result) {return isDeferred(result.status)?StepStatus::Blocked:result?StepStatus::Complete:StepStatus::NotImplemented;};
    const auto idle=[&](int frames) {
        perform(controller,[this,id] {ports_.monsters.neutralMode(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};});
        controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));
        return StepStatus::Complete;
    };
    auto target=acquireTarget(unit,tick,false,false);
    const auto cast=[&](size_t slot,std::optional<Vec> position={},bool coordinate=false,std::optional<int> wait={},int mode=-1) {
        const auto skill=unit.rule.skillIds.at(slot);const auto action=unit.rule.skillActions.find(skill);
        if(!skill) return usePd2Skill(controller,{id,UINT16_MAX,{},tick.tick,1,position},wait,true);
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,coordinate?UnitTarget{}:target?target->unit:UnitTarget{},tick.tick,
            uint8_t(mode<0?action->second.nativeMode:mode),position},wait,true);
    };
    const auto values=ports_.effects.unitStatValues(id,tick.tick);if(!values) return StepStatus::Blocked;
    const auto stat=[&](std::string_view name){return nativeStatValue(*values,*unit.rule.stats,name);};
    const auto write=[&](std::string name,int value) {return setPd2AiStats(controller,tick,{{std::move(name),value}});};
    const auto phaseState=[&](bool active) {return perform(controller,[this,id,active,tick] {return ports_.effects.pd2LucionPhaseState(id,active,tick.tick);});};
    const auto presence=[&](bool active) {return perform(controller,[this,id,active] {return ports_.monsters.pd2PhasePresence(id,active);});};
    const auto ordinary=[&](int mode) {return status(requestAction(controller,{id,0,target?target->unit:UnitTarget{},tick.tick,uint8_t(mode)}));};
    const auto spawn=[&](int definition,Vec requested,std::function<DomainResult<>(bool)> completion={}) {
        auto position=ports_.world.nativeFreeSpace(unit.area,unit.position,requested,3);
        if(!position) position=world::QuestPosition{unit.area,requested};
        return perform(controller,[this,id,definition,position,tick,completion=std::move(completion),done=false,born=false]() mutable ->DomainResult<> {
            if(!done) {
                const auto child=ports_.monsters.admitPd2NativeChild(id,position->area,position->position,definition,1,0x4020000,true,
                    tick.tick,ports_.spatial.nativeCollisionSample());
                if(isDeferred(child.status) || child.status==DomainStatus::NotImplemented) return {child.status,{}};
                born=child && bool(*child.value);done=true;
            }
            return completion?completion(born):DomainResult<>{DomainStatus::Applied,std::monostate{}};
        });
    };
    if(type!=1112) {
        if(map.pauseDepth>0) return idle(10);
        const auto bossId=map.bindings[0].id;
        const auto bossControl=state_.controllers.find(bossId);
        const bool active=bossControl!=state_.controllers.end() && bossControl->second.phase==5;
        const int scene=map.phase;
        const auto roll=draw()%100;
        int delay=ai.pd2Delays[size_t(unit.rule.difficulty)];
        const auto cooldownCompletion=[&,id,tick](int cooldown) {
            return [this,id,tick,cooldown](bool born)->DomainResult<> {
                if(!born) return {DomainStatus::Applied,std::monostate{}};
                return setPd2AiStats(state_.controllers.at(id),tick,{{"mon_cooldown1",cooldown}});
            };
        };
        if(type==1113) {
            if(scene>=3) {
                int base=parameter(1,1);if(active) {delay>>=1;base-=3;}
                const int cooldown=stat("mon_cooldown1");
                if(cooldown<1 && active) {
                    int count=0;std::vector<EntityId> remove;
                    for(const auto &[otherId,other]:ports_.monsters.read().actors) if(other.area==RegionId(188) && otherId!=id && other.identity.nativeClass!=1112) {
                        if(other.nativeMode==12) remove.push_back(otherId);
                        else if(other.identity.nativeClass==1141) ++count;
                    }
                    for(const auto other:remove) perform(controller,[this,other] {return ports_.monsters.remove(other);});
                    if(count<parameter(1,2) && int(roll)<100+(difficulty-parameter(1,0))*count) {
                        if(controller.randomPublished) {
                            controller.random=controller.randomExpected=unit.combatRandom;controller.randomPublished=false;
                        }
                        const int x=int(draw()%44)+0x620d;
                        const int y=int(draw()%44)+0x17cb;
                        const auto result=spawn(1141,Vec{float(x),float(y)}-area.definition.origin,
                            cooldownCompletion(int(roll)%int(3-difficulty)-(scene>>1)+base));
                        if(!result) return status(result);
                    }
                } else {const auto result=write("mon_cooldown1",nativeSubtract(cooldown,1));if(!result) return status(result);}
            }
            if(scene>3) {
                const int cooldown=stat("mon_cooldown2");
                if(cooldown<1) {
                    const auto result=cast(0);
                    write("mon_cooldown2",int(roll)%int(3-difficulty)-(scene>>1)+parameter(2,0)-difficulty+(active?2:0));
                    return result;
                }
                write("mon_cooldown2",nativeSubtract(cooldown,1));
            }
        } else {
            int base=parameter(3,0);if(active) {base>>=1;--delay;}
            if(scene>2) {
                const int cooldown=stat("mon_cooldown1");
                if(cooldown<1) {
                    std::array<int,3> counts{};
                    for(const auto &[otherId,other]:ports_.monsters.read().actors)
                        if(otherId!=id && other.area==RegionId(188) && other.nativeMode!=0 && other.nativeMode!=12) {
                            if(other.identity.nativeClass==1114) ++counts[0];
                            if(other.identity.nativeClass==1115) ++counts[1];
                            if(other.identity.nativeClass==1140) ++counts[2];
                        }
                    const int choice=int(draw()%100);
                    int definition=counts[0]<parameter(2,0)-(scene>>1)?1114:-1;
                    if(scene>=5 && choice<parameter(1,0) && counts[1]<parameter(2,1)-scene/3) definition=1115;
                    else if(scene>=6 && choice<parameter(1,1) && counts[2]<parameter(2,2)) definition=1140;
                    if(definition>=0) {
                        const auto result=spawn(definition,unit.position,
                            cooldownCompletion(int(roll)%int(3-difficulty)-(scene>>1)+base));
                        if(!result) return status(result);
                    }
                } else {const auto result=write("mon_cooldown1",nativeSubtract(cooldown,1));if(!result) return status(result);}
            }
        }
        return idle(delay);
    }
    ports_.maps.bind(0,id,unit.area,unit.position,false);
    int nearest=INT32_MAX;std::optional<NativeAiTarget> nearestPlayer;
    for(const auto playerId:ports_.players.nativeActorOrder()) {
        const auto *player=ports_.players.findActor(playerId);
        if(!player || !player->entered || player->area!=unit.area || player->persistent.player.hp<=0) continue;
        const int x=nativeSubtract(int(player->position.x),int(unit.position.x)),y=nativeSubtract(int(player->position.y),int(unit.position.y));
        const auto squared=nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
        if(squared<nearest) {nearest=squared;nearestPlayer=NativeAiTarget{UnitTarget{playerId,0,0},player->position,2,0};}
    }
    if(map.pauseDepth>0) {
        if(nearestPlayer) ports_.maps.consumeScenePause();
        const auto guard=setPd2Guard(controller,tick,true);if(!guard) return status(guard);
        return idle(10);
    }
    if(!target) target=nearestPlayer;
    if(!target) return idle(10);
    const int delay=int(ai.pd2Delays[size_t(unit.rule.difficulty)])-difficulty;
    const auto before=map.phase;const int life=playerLifePercentage(unit.life,unit.maximumLife);
    if((life<(difficulty+17)*5 && before<2) || (life<(difficulty+16)*5 && before<3) ||
       (life<difficulty*4+75 && before<4) || (life<difficulty*4+70 && before<5) ||
       (life<difficulty*3+55 && before<6) || (life<difficulty*3+35 && before<7)) ports_.maps.scenePhase(uint8_t(before+1));
    const auto cooldown=map.sceneCooldown;
    if(cooldown && controller.phase!=5) ports_.maps.sceneCooldown(uint8_t(cooldown-1));
    const int nova=stat("mon_cooldown1");
    if(nova>0 && controller.phase!=7 && controller.phase!=8) write("mon_cooldown1",nativeSubtract(nova,1));
    const int previous=controller.phase;
    const auto next=[&](int phase,int loop=0) {controller.phase=phase;controller.loop=loop;controller.thirdParameter=previous;};
    const auto select=[&](int phase) {idle(delay);next(phase);return StepStatus::Complete;};
    bool crossing=map.phase==3 && before<3;
    if(crossing) controller.phase=4;
    if(!crossing && controller.phase==1) {
        const int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
        if(nearest>700 || (nearest>500 && roll<20)) controller.phase=10;
    }
    if(crossing || (controller.phase==4 && controller.loop==0)) {
        const auto guard=setPd2Guard(controller,tick,false);if(!guard) return status(guard);
        perform(controller,[this,id,tick] {return ports_.monsters.aiPose(id,10,false,false,tick.tick,25);});
        controller.nextDecision=tick.tick+25;
        if(crossing) for(const auto point:std::array<Vec,4>{{{0x622d,0x17eb},{0x622d,0x17d5},{0x6217,0x17d5},{0x6217,0x17eb}}}) {
            const auto born=spawn(1141,point-area.definition.origin);if(!born) return status(born);
        }
        next(4,1);return StepStatus::Complete;
    }
    switch(controller.phase) {
    case 0:idle(40);if(!map.phase) ports_.maps.scenePhase(1);next(6);return StepStatus::Complete;
    case 1: {
        const auto guard=setPd2Guard(controller,tick,true);if(!guard) return status(guard);
        const int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
        if(roll<parameter(1,0)) {
            const int extra=int(pd2CrtRandom(ports_.pd2Random)%100);
            const bool combat=unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,target->position,target->size,unit.rule.meleeRange);
            if(combat) return status(perform(controller,[this,id,victim=target->unit.id,tick] {return ports_.effects.missileReaction(victim,id,false,true,0,tick.tick);}));
            if(nearest>99 || extra>29) return select(9);
            const auto moved=perform(controller,[this,id,point=target->position,region=unit.area,generation=area.generation,tick] {
                const auto result=ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
                if(isDeferred(result.status)) return result;
                auto &control=state_.controllers.at(id);control.pursuing=bool(result);control.nextDecision=tick.tick+15;
                return DomainResult<>{DomainStatus::Applied,std::monostate{}};
            });
            next(1);return status(moved);
        }
        if(roll<parameter(1,1)) return select(12);
        if(controller.thirdParameter!=8 && roll<parameter(1,2) && map.phase>1 && nova<1) return select(7);
        if(controller.thirdParameter!=6 && roll<parameter(2,0) && map.phase>=3 && !cooldown) return select(4);
        return select(map.phase<2 && roll<parameter(1,2)?9:3);
    }
    case 3: {const auto result=ordinary(5);next(1);return result;}
    case 4: {
        const auto state=phaseState(true);if(!state) return status(state);
        const auto hidden=presence(false);if(!hidden) return status(hidden);
        const int count=int(pd2CrtRandom(ports_.pd2Random)%6)+6;
        const auto eclipse=perform(controller,[this,region=unit.area,tick] {return ports_.maps.sceneEclipse(region,true,tick);});
        ports_.maps.sceneCooldown(0);idle(delay);next(5,count);return status(eclipse);
    }
    case 5: {
        int loops=controller.loop;
        if(loops<=0) {ports_.maps.sceneCooldown(uint8_t(parameter(3,0)));const auto result=ordinary(4);next(6);return result;}
        if(!cooldown) {
            --loops;
            if(pd2CrtRandom(ports_.pd2Random)%100<60) {const auto result=ordinary(4);next(5,loops);return result;}
            ports_.maps.sceneCooldown(1);
        } else if(cooldown==1) ports_.maps.sceneCooldown(2);
        else if(cooldown==2) ports_.maps.sceneCooldown(3);
        else {ports_.maps.sceneCooldown(0);const auto result=ordinary(4);next(5,loops-1);return result;}
        const auto result=cast(3,target->position);next(5,loops);return result;
    }
    case 6:
        if(!controller.loop) {
            const auto result=perform(controller,[this,region=unit.area,tick] {return ports_.maps.sceneEclipse(region,false,tick);});
            idle(delay);next(6,1);return status(result);
        }
        if(controller.loop==1) {
            const auto state=phaseState(false);if(!state) return status(state);
            const auto restored=presence(true);if(!restored) return status(restored);
            const auto pose=perform(controller,[this,id,tick] {return ports_.monsters.aiPose(id,11,false,false,tick.tick,10);});
            controller.nextDecision=tick.tick+10;next(6,2);return status(pose);
        }
        {const auto guard=setPd2Guard(controller,tick,false);if(!guard) return status(guard);return select(1);}
    case 7:
        if(!controller.loop) {const auto result=cast(0,Vec{0x6223,0x17e1}-area.definition.origin,true,35-difficulty*4);next(7,1);return result;}
        if(controller.loop==1) {
            const int count=int(draw()%10000)+1;
            const auto result=cast(1,{},true,65-difficulty*8);next(7,count);return result;
        }
        {const int count=controller.loop;const auto result=cast(1,{},true,{},9);next(8,count);return result;}
    case 8: {
        const auto value=int(draw()%uint32_t((3-difficulty)*2))-(map.phase>>1)+parameter(3,1);
        const auto written=write("mon_cooldown1",value);idle(delay);next(1);return status(written);
    }
    case 9: {const auto result=cast(2,target->position,true);next(1);return result;}
    case 10: {
        const int x=int(target->position.x)-1+int(pd2CrtRandom(ports_.pd2Random)%2);
        const int y=int(target->position.y)-1+int(pd2CrtRandom(ports_.pd2Random)%2);
        const auto result=cast(0,Vec{float(x),float(y)},true);next(1);return result;
    }
    case 12: {
        const auto point=target->position+Vec{target->position.x>unit.position.x?3.f:-3.f,target->position.y>unit.position.y?3.f:-3.f};
        const auto result=cast(4,point,true);next(1);return result;
    }
    default:return select(1);
    }
}
}
