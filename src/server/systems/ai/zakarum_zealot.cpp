#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/quest/state.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::zakarumZealotFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;auto &random=controller.random;
    const int runVelocity=75+unit.rule.ai.retreatVelocityBonus;
    const auto chance=[&](int percent) {return int(limitedRandom(random,100))<percent;};
    const bool retaliating=unit.hitOccurrence!=controller.observedHit;controller.observedHit=unit.hitOccurrence;
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    const auto move=[&](Vec goal,EntityId victim,bool running,int velocity,int distance=5) {
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,goal},victim,0,velocity,running,1,0,0,13,uint8_t(distance)});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
    };
    const auto wander=[&](int radius) {return move(monsterWanderPoint(unit.position,radius,random),{},false,75);};
    const auto walk=[&] {
        if(!move(point,target?target->id:EntityId{},false,75)) {
            if(limitedRandom(random,100)>=70) controller.nextDecision=tick.tick+10;else wander(4);
        }
    };
    const auto circle=[&] {
        const int turn=(rollRandom(random)&255)<128?1:-1;
        const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,0,turn,4});
        controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);
    };
    const auto escape=[&] {return move(monsterRetreatPoint(unit.position,point,8),{},true,runVelocity,8);};
    const PlayerState *player=target?ports_.players.findActor(target->id):nullptr;
    if(target && target->type==1) if(const auto *victim=ports_.monsters.find(target->id);victim && victim->owner) player=ports_.players.find(*victim->owner);
    if(player && area.definition.act==3 && player->persistent.player.quests.at(size_t(unit.rule.difficulty)).at(questIndex(QuestId::BlackenedTemple)).stage==4) {
        if(!escape()) wander(6);return StepStatus::Complete;
    }
    if(retaliating) {
        if(!controller.loop && unit.life*100/unit.maximumLife<params[2]) {
            controller.phase=0;if(escape()) return StepStatus::Complete;controller.loop=5;
        }
        const auto global=unit.position+area.definition.origin;
        const auto room=ports_.areas.roomAtPosition(unit.area,unit.position,int(std::floor(global.x)),int(std::floor(global.y)));
        if(room && (ports_.spatial.nativeCollisionSample()(*room,int(std::floor(global.x)),int(std::floor(global.y)))&0x40)) {
            if(contact) circle();else walk();return StepStatus::Complete;
        }
    }
    if(controller.loop) --controller.loop;
    if(!contact) {
        controller.phase=0;
        if(chance(params[3])) move(point,target?target->id:EntityId{},true,runVelocity);else walk();
        return StepStatus::Complete;
    }
    if(controller.phase && !chance(params[0])) {
        controller.phase=0;
        if(limitedRandom(random,100)>=80) circle();
        else {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+10;}
        return StepStatus::Complete;
    }
    controller.phase=1;const auto mode=uint8_t(chance(params[1])?5:4);
    const auto result=requestAction(controller,{id,0,*target,tick.tick,mode});
    controller.nextDecision=tick.tick+(result?1:10);
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
