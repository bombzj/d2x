#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::mephistoFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto primary=target.value_or(UnitTarget{});
    const auto percentage=[&]{return int(limitedRandom(controller.random,100));};
    const auto idle=[&](int delay){ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(delay);};
    const auto move=[&](Vec at,EntityId victim,int velocity=75,int turn=0,int circle=0,uint8_t path=13,uint8_t bound=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,velocity,false,1,turn,circle,path,bound});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    const auto wander=[&](Vec center,int radius,int velocity=75){return move(monsterWanderPoint(center,radius,controller.random),{},velocity);};
    const auto circle=[&](int radius,int velocity=75) {
        const int turn=(rollRandom(controller.random)&255)<128?1:-1;
        return move(point,primary.id,velocity,turn,radius,13,uint8_t(radius));
    };
    const auto walk=[&] {
        if(!move(point,primary.id,75,0,0,13,4)) {
            if(percentage()<70) wander(unit.position,4);else idle(10);
        }
    };
    const auto cast=[&](int slot,UnitTarget victim) {
        const auto skill=slot<0?uint16_t{}:unit.rule.skillIds[size_t(slot)];
        const auto mode=slot<0?uint8_t{4}:unit.rule.skillActions.at(skill).nativeMode;
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const int life=playerLifePercentage(unit.life,unit.maximumLife),lifeChance=std::max((100-life)/5,0);
    int state=controller.thirdParameter;
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    const auto spacing=[&] {
        if(percentage()>=65) idle(10);
        else if(percentage()<65 || distance>5) circle(4);
        else walk();
        controller.thirdParameter=0;
    };
    if(contact) {
        if(percentage()<=params[0]) {state=2;controller.phase=int(rollRandom(controller.random)%3)+3;}
        else state=3;
    } else if(distance<=20) {
        if(!controller.thirdParameter) {
            if(life<=20 && distance<5 && percentage()<40) state=1;
            else {
                if(percentage()>=lifeChance+50) {spacing();return StepStatus::Complete;}
                state=2;controller.phase=int(rollRandom(controller.random)%3)+3;
            }
        }
    } else state=4;
    switch(state) {
    case 0:idle(5);controller.thirdParameter=4;return StepStatus::Complete;
    case 1:
        state=0;
        if(target && move(monsterRetreatPoint(unit.position,point,8),{},50,0,0,2,8)) {controller.thirdParameter=state;return StepStatus::Complete;}
        if(contact) {controller.thirdParameter=state;return cast(2,primary);}
        break;
    case 2:break;
    case 3:
        controller.thirdParameter=0;
        if(percentage()>=lifeChance+80) {
            if(!circle(3,50)) wander(unit.position,12);
            return StepStatus::Complete;
        }
        return cast(percentage()>=80-lifeChance?2:-1,primary);
    case 4:
        if(!wander(point,6,50)) {
            if(!move(monsterRadiusApproachPoint(unit.position,unit.rule.size,point,12,6),{})) wander(unit.position,12);
        }
        controller.thirdParameter=0;return StepStatus::Complete;
    default:spacing();return StepStatus::Complete;
    }
    controller.phase=nativeSubtract(controller.phase,1);if(!controller.phase) state=0;
    if(!controller.loop) {
        controller.loop=2;
        if(!circle(3)) wander(unit.position,12);
        controller.thirdParameter=state;return StepStatus::Complete;
    }
    controller.loop=nativeAdd(controller.loop,1);
    // The retail callback's third candidate checks the source's unit type.
    // Mephisto is a monster, so only the lowest-life candidate and aip2 roll
    // can replace the primary; aip3 does not add a second selection roll.
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    std::optional<UnitTarget> lowest;int lowestLife=INT32_MAX;
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
                const auto other=participants.find(candidate);
                if(candidate==id || !other.aliveIn(near.area) || !other.damageable() || !participants.canHarm(id,candidate)) continue;
                const auto at=other.position()+ports_.areas.at(near.area).definition.origin-area.definition.origin;
                const int x=int(std::floor(at.x))-int(std::floor(unit.position.x)),y=int(std::floor(at.y))-int(std::floor(unit.position.y));
                if(nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y))>1024) continue;
                const int hp=nativeNarrow(uint32_t(other.player?other.player->persistent.player.hp.raw():other.monster->life))>>8;
                if(hp<lowestLife) {lowestLife=hp;lowest=UnitTarget{candidate,0,uint8_t(other.monster?1:0)};}
            }
    auto victim=primary;if(lowest && percentage()<params[1]) victim=*lowest;
    int count{};while(count<8 && unit.rule.skillIds[size_t(count)]) ++count;
    if(!count) {wander(unit.position,6);controller.thirdParameter=state;return StepStatus::Complete;}
    const int skillChance=100/count,roll=percentage();
    const auto recipient=participants.find(victim.id);
    const auto at=recipient.id()?recipient.position()+ports_.areas.at(recipient.player?recipient.player->area:recipient.monster->area).definition.origin-area.definition.origin:point;
    const bool blockedRay=unit.rule.difficulty && recipient.id() && !area.definition.collision.missileSegment(unit.position,at,{4,1});
    const int slot=unit.rule.difficulty && (blockedRay || distance>30)?5:
        distance<15 && roll<skillChance && unit.rule.difficulty?4:roll<2*skillChance?3:roll<3*skillChance?1:0;
    const auto skill=unit.rule.skillIds[size_t(slot)];const auto mode=unit.rule.skillActions.at(skill).nativeMode;
    // UseSkill/ModeChange may advance this same unit seed. The trailing AI
    // roll must run on that published seed, after a retained cast completes.
    const auto result=perform(controller,[this,id,skill,mode,victim,tick,state,lifeChance] {
        const auto casted=ports_.skills.requestCast({id,skill,victim,tick.tick,mode});
        if(isDeferred(casted.status)) return casted;
        if(const auto *current=ports_.monsters.find(id)) {
            const auto expected=current->combatRandom;auto random=expected;
            const bool reset=int(limitedRandom(random,100))<50-lifeChance;
            const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
            auto &ai=state_.controllers.at(id);if(reset) ai.loop=0;ai.thirdParameter=state;
        }
        return casted;
    });
    controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
