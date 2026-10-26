#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <memory>

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2RathmaBoss(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &map=ports_.maps.read();const auto type=unit.rule.nativeClass;
    if(type<933 || type>936 || !map.instance || map.instance->controller!=43) return {};
    const auto &area=ports_.areas.at(unit.area);const auto &ai=unit.rule.ai;
    ports_.maps.bind(type==936?1:0,id,unit.area,unit.position,false);
    if(!map.phase) ports_.maps.scenePhase(1);
    const bool poison=type==934 || type==936;
    const auto parameter=[&](size_t index,size_t column){return ai.pd2Parameters.at(index-1).at(column);};
    const int delay=std::max(3,int(ai.pd2Delays[1])*(map.phase<6?2:1)-int(map.instance->bossDifficulty)*4+1);
    auto target=acquireTarget(unit,tick,false,false);
    int nearest=INT32_MAX,farthest=0;std::optional<NativeAiTarget> nearestPlayer;
    for(const auto playerId:ports_.players.nativeActorOrder()) {
        const auto *player=ports_.players.findActor(playerId);
        if(!player || !player->entered || player->area!=unit.area || player->persistent.player.hp<=0) continue;
        const int x=nativeSubtract(int(player->position.x),int(unit.position.x)),y=nativeSubtract(int(player->position.y),int(unit.position.y));
        const auto distance=nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
        if(distance>farthest && distance<1500) farthest=distance;
        if(distance<nearest) {nearest=distance;nearestPlayer=NativeAiTarget{UnitTarget{playerId,0,0},player->position,2,0};}
    }
    const auto status=[](DomainResult<> result) {return isDeferred(result.status)?StepStatus::Blocked:result?StepStatus::Complete:StepStatus::NotImplemented;};
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));return StepStatus::Complete;};
    if(map.pauseDepth>0) {
        if(nearestPlayer && (map.phase!=5 || map.bindings[0].id==id)) ports_.maps.consumeScenePause();
        ports_.maps.sceneCooldown(0);const auto guarded=setPd2Guard(controller,tick,true);if(!guarded) return status(guarded);
        return idle(10);
    }
    if(!target) target=nearestPlayer;
    if(!target) return idle(25);
    const auto values=ports_.effects.unitStatValues(id,tick.tick);if(!values) return StepStatus::Blocked;
    const auto stat=[&](std::string_view name) {return nativeStatValue(*values,*unit.rule.stats,name);};
    const auto write=[&](std::vector<std::pair<std::string,int32_t>> values) {return setPd2AiStats(controller,tick,std::move(values));};
    const auto draw=[&](uint32_t bound) {
        if(controller.randomPublished) {controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;controller.randomPublished=false;}
        return int(limitedRandom(controller.random,bound));
    };
    const auto cast=[&](size_t slot,std::optional<Vec> point={},std::optional<int> wait={}) {
        const auto skill=unit.rule.skillIds.at(slot);const auto action=unit.rule.skillActions.find(skill);
        if(!skill) return usePd2Skill(controller,{id,UINT16_MAX,{},tick.tick,1,point},wait,true);
        if(action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,point?UnitTarget{}:target->unit,tick.tick,action->second.nativeMode,point},wait,true);
    };
    const auto center=[&] {
        return (map.phase<3?Vec{0x476c,0x35bf}:map.phase<5?Vec{0x47d3,0x394b}:Vec{0x4e9a,0x1403})-area.definition.origin;
    };
    const auto blinkCenter=[&](int phase,int loop) {
        const auto result=cast(7,center());controller.phase=phase;controller.loop=loop;controller.thirdParameter=phase;return result;
    };
    const auto heal=[&] {
        return perform(controller,[this,id,tick] {
            const auto &source=*ports_.monsters.find(id);
            attributes::MonsterResourceTrace trace(source,tick.tick,ports_.effects.unitEffects(id));trace.sampleSources(ports_.effects.unitStatSources(id,tick.tick));
            trace.seedLife(Fixed8::fromRaw(pd2MultiplyDivide(trace.read().maximum.raw(),70.,100)));
            return ports_.monsters.commitLife(trace.take())?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Stale,{}};
        });
    };
    bool extra=false,pillar=false;
    std::vector<EntityId> corpses;
    for(const auto &[otherId,other]:ports_.monsters.read().actors) {
        if(otherId==id) continue;
        if(other.identity.nativeClass>=340 && other.identity.nativeClass<=343 && other.nativeMode==12) corpses.push_back(otherId);
        if(other.area!=unit.area || other.nativeMode==12) continue;
        if(other.identity.nativeClass==968 || other.identity.nativeClass==969 || other.identity.nativeClass==1106 || other.identity.nativeClass==1107) extra=true;
        if(other.identity.nativeClass==938) {pillar=true;break;}
    }
    for(const auto corpse:corpses) perform(controller,[this,corpse] {return ports_.monsters.remove(corpse);});
    if(map.phase>5 && !ports_.effects.unitStates(id,tick.tick).contains(103)) {
        const auto posted=perform(controller,[this,id,tick] {return ports_.effects.pd2SceneSkillState(id,id,485,1,tick.tick);});
        if(!posted) return status(posted);
    }
    int phase=controller.phase,loop=controller.loop;const int previousPhase=controller.thirdParameter;
    const int life=playerLifePercentage(unit.life,unit.maximumLife),stage=stat("mon_cooldown1"),weight=stat("mon_cooldown3");
    bool guarded=false;
    if(life<15 && (map.phase==1 || map.phase==3)) {pillar=false;phase=6;if(controller.phase!=6) loop=0;guarded=true;}
    else if(poison && (map.phase==1 || map.phase==5)) guarded=pillar || phase==31 || (phase==41 && loop<3);
    else if(!poison || map.phase>4) guarded=extra || phase==33;
    if(nearest>parameter(1,0)) {if(nearestPlayer) target=nearestPlayer;guarded=true;}
    // Each threshold is a separate native early return. Reaching several
    // thresholds in one hit never emits several summon waves at once.
    const auto threshold=[&](int nextStage,int nextPhase,bool replenish) ->StepStatus {
        write({{"mon_cooldown1",nextStage}});
        if(replenish) {const auto restored=heal();if(!restored) return status(restored);}
        const auto guard=setPd2Guard(controller,tick,!poison && nextPhase==32?guarded:true);if(!guard) return status(guard);
        ports_.maps.sceneCooldown(0);
        if(nextPhase==31 || nextPhase==41) return blinkCenter(nextPhase,1);
        write({{"mon_cooldown2",nextPhase}});const auto result=cast(6);controller.phase=1;controller.loop=0;controller.thirdParameter=nextPhase;return result;
    };
    if(!poison) {
        if(life<90 && stage<1) write({{"mon_cooldown1",1}});
        else if(life<80 && stage<2) return threshold(2,32,false);
        else if(life<70 && stage<3) return threshold(3,33,true);
        else if(life<60 && stage<4) write({{"mon_cooldown1",4}});
        else if(life<50 && stage<5) return threshold(5,33,true);
        else if(life<40 && stage<6) write({{"mon_cooldown1",6}});
        else if(life<30 && stage<7) return threshold(7,33,true);
        else if(life<15 && stage<8 && map.phase<5) {write({{"mon_cooldown1",8}});heal();phase=6;loop=0;guarded=true;}
        else if(life<20 && stage<8 && map.phase>4) write({{"mon_cooldown1",8}});
    } else {
        if(life<90 && stage<1 && type==934) return threshold(1,31,true);
        if(life<80 && stage<2) return threshold(2,41,false);
        if(life<70 && stage<3 && type==934) return threshold(3,31,true);
        if(life<60 && stage<4) return threshold(4,41,false);
        if(life<50 && stage<5 && type==934) return threshold(5,31,true);
        if(life<40 && stage<6) return threshold(6,41,false);
        if(life<30 && stage<7 && type==934) return threshold(7,31,true);
        if(life<15 && stage<8 && map.phase<5) {write({{"mon_cooldown1",8}});heal();phase=6;loop=0;guarded=true;}
        else if(life<20 && stage<8 && map.phase>4) return threshold(8,41,false);
    }
    const auto guard=setPd2Guard(controller,tick,guarded);if(!guard) return status(guard);
    ports_.maps.sceneCooldown(0);
    const auto next=[&](int selected,int iteration=0) {controller.phase=selected;controller.loop=iteration;controller.thirdParameter=phase;};
    const auto select=[&](int selected,int iteration=0) {idle(delay);next(selected,iteration);return StepStatus::Complete;};
    if(phase==0) {setPd2Guard(controller,tick,true);idle(62);next(1);return StepStatus::Complete;}
    if(phase==2) {write({{"mon_cooldown3",0}});idle(delay/2);next(1);return StepStatus::Complete;}
    if(phase==3) {
        const int radius=5+int(pd2CrtRandom(ports_.pd2Random)%5);
        if(controller.randomPublished) {controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;controller.randomPublished=false;}
        const auto point=monsterWanderPoint(unit.position,radius,controller.random);
        const auto moved=perform(controller,[this,id,point,region=unit.area,generation=area.generation] {
            const auto result=ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
            return isDeferred(result.status)?result:DomainResult<>{DomainStatus::Applied,std::monostate{}};
        });
        write({{"mon_cooldown3",0}});controller.pursuing=bool(moved);controller.nextDecision=tick.tick;next(1);return status(moved);
    }
    if(phase==4) {const auto moved=walkToTargetWithFallback(controller,target->unit,target->position,tick);next(1);return status(moved);}
    if(phase==5) {
        if(pillar) {const auto result=cast(0);next(1);return result;}
        // Both pairs are consumed below 50 HP before the wider replacement.
        auto point=Vec{float(int(target->position.x)-8+int(pd2CrtRandom(ports_.pd2Random)%16)),
            float(int(target->position.y)-8+int(pd2CrtRandom(ports_.pd2Random)%16))};
        if(life<50) point=Vec{float(int(target->position.x)-16+int(pd2CrtRandom(ports_.pd2Random)%32)),
            float(int(target->position.y)-16+int(pd2CrtRandom(ports_.pd2Random)%32))};
        const auto result=cast(7,point,!poison && loop==1?std::optional{5}:std::nullopt);
        write({{"mon_cooldown3",0}});next(!poison && loop==1?11:1);return result;
    }
    if(phase==6 && type<=934) {
        if(!loop) {
            const auto result=cast(7,center()+Vec{4,0});
            for(const auto &[otherId,other]:ports_.monsters.read().actors) if(other.nativeMode!=12 &&
                (other.identity.nativeClass==937 || other.identity.nativeClass==938 || other.identity.nativeClass==939 || other.identity.nativeClass==966 ||
                 other.identity.nativeClass==968 || other.identity.nativeClass==969 || other.identity.nativeClass==1106 || other.identity.nativeClass==1107))
                    perform(controller,[this,otherId,tick] {return ports_.monsters.scriptDeath(otherId,tick.tick,{},true);});
            next(6,1);return result;
        }
        if(loop==1) {
            const auto portal=perform(controller,[this,id,tick] {return ports_.maps.rathmaRetreatPortal(id,tick);});
            if(!portal) return status(portal);
            write({{"mon_cooldown2",0}});const auto result=cast(6,center(),25);next(6,2);return result;
        }
        if(loop==2) {
            const auto point=unit.position-Vec{4,0};
            const auto moved=perform(controller,[this,id,point,region=unit.area,generation=area.generation] {
                const auto result=ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
                return isDeferred(result.status)?result:DomainResult<>{DomainStatus::Applied,std::monostate{}};
            });
            controller.nextDecision=tick.tick+50;next(6,3);return status(moved);
        }
        const auto removed=perform(controller,[this,id] {return ports_.monsters.remove(id);});next(1);return status(removed);
    }
    if(phase>=10 && phase<=15) {const auto result=cast(size_t(phase-10));next(1);return result;}
    if(phase==16 || phase==30 || phase==32 || phase==33) {
        if(phase>=30) write({{"mon_cooldown2",phase}});
        const auto result=cast(6);next(1);return result;
    }
    if(phase==17) {if(pillar) return select(1);const auto result=cast(7);next(1);return result;}
    if(phase==31) {
        if(!loop) return blinkCenter(31,1);
        if(loop==1) {write({{"mon_cooldown2",31}});const auto result=cast(6);next(10);return result;}
        next(31);return StepStatus::Complete;
    }
    if(phase==40) {
        const int index=loop;
        StepStatus result=StepStatus::Complete;
        if(index==0 || index==2 || index==4) {
            const auto x=float(int(target->position.x)-8+int(pd2CrtRandom(ports_.pd2Random)%16));
            const auto y=float(int(target->position.y)-8+int(pd2CrtRandom(ports_.pd2Random)%16));
            result=cast(7,Vec{x,y},5);
        }
        else if(index==1 || index==3 || index==5) result=cast(0);
        write({{"mon_cooldown3",0}});next(index==5?5:40,index<5?index+1:0);return result;
    }
    if(phase==41) {
        if(!loop) return blinkCenter(41,1);
        if(loop==1) {const auto result=cast(5);next(41,2);return result;}
        if(loop==2) {const auto result=cast(2);next(41,3);return result;}
        if(loop>=3 && loop<=8) {
            ports_.maps.sceneCooldown(1);const int choice=draw(100);
            const auto result=cast(choice<parameter(3,0)+parameter(3,1)?1:choice<parameter(3,0)+parameter(3,1)+parameter(3,2)?4:0,{},28);
            next(41,loop+1);return result;
        }
        const auto result=cast(3);ports_.maps.sceneCooldown(0);next(5);return result;
    }
    if(phase==42) {
        const auto skill=unit.rule.skillIds[2];
        const auto state=perform(controller,[this,id,victim=target->unit.id,skill,rank=ai.pd2SkillRanks[2],tick] {
            return ports_.effects.pd2SceneSkillState(id,victim,skill,rank,tick.tick);
        });
        if(!state) return status(state);
        const auto result=cast(2);next(loop==1?5:1,loop==1?1:0);return result;
    }
    // Native default chooses a later callback; it does not cast immediately.
    if(!poison && farthest>parameter(1,0) && previousPhase!=5 && previousPhase!=11 && previousPhase!=42 && !pillar) return select(42,1);
    if(farthest>parameter(1,0) && !pillar) return select(5);
    const int chance=draw(100),choice=draw(100),total=nativeAdd(parameter(1,2),weight);
    if(chance<total && !pillar) {
        // The original divisions are by this positive accumulated weight.
        if(chance<parameter(2,1)/total) return select(2);
        return select(chance<(parameter(2,1)+parameter(2,2))/total?3:5);
    }
    write({{"mon_cooldown3",pillar?100:nativeAdd(parameter(2,0),weight)}});
    if(poison) {
        if(choice<parameter(3,0)) return select(12);
        if(choice<parameter(3,0)+parameter(3,1)) return select(11);
        if(choice<parameter(3,0)+parameter(3,1)+parameter(3,2) && !pillar) return select(14);
        return select(10);
    }
    if(choice<parameter(3,0) && previousPhase!=42) return select(42);
    const int first=parameter(3,0)+parameter(3,1),second=first+parameter(3,2);
    if((choice<second && stage<2) || choice<first) return select(14);
    if(choice<second && stage>=2) return select(32);
    if(choice<second+parameter(4,0)) return select(11);
    return select(choice<second+parameter(4,0)+parameter(4,1)?40:10);
}
}
