#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace d2x::server::ai {
StepStatus System::pd2Warlord(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    ports_.maps.bind(0,id,unit.area,unit.position,false);
    const bool combat=target && unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange);
    const auto global=unit.position+area.definition.origin;
    const auto squared=[](Vec first,Vec second) {
        const int x=int(std::floor(first.x))-int(std::floor(second.x)),y=int(std::floor(first.y))-int(std::floor(second.y));
        return nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
    };
    // Fixed arena coordinates in the current native program, not generated
    // terrain or a replacement spawn point. All content stays MPQ-owned.
    constexpr Vec center{float(0x5a4c),float(0x25fe)};
    const int bossDistance=squared(global,center);
    int nearestDistance=INT32_MAX,playerDistance=INT32_MAX;
    for(const auto actor:ports_.players.nativeActorOrder()) {
        const auto *player=ports_.players.findActor(actor);
        if(!player || !player->entered || player->area!=unit.area || player->persistent.player.hp<=0 || !ports_.areas.hasRoomUnit(actor)) continue;
        const auto position=player->position+area.definition.origin;
        const int distance=squared(position,global);
        if(distance<nearestDistance) {
            nearestDistance=distance;playerDistance=squared(position,center);
            target=UnitTarget{actor,0,0};point=player->position;
        }
    }
    Vec returnPoint=center;
    if(bossDistance>500) {
        const double x=double(center.x)-global.x,y=double(center.y)-global.y;
        const double length=std::hypot(x,y);
        if(length) returnPoint={float(int(double(global.x)+x*20./length)),float(int(double(global.y)+y*20./length))};
        const auto free=ports_.world.nativeFreeSpace(unit.area,unit.position,returnPoint-area.definition.origin,unit.rule.size);
        if(free && free->area==unit.area) returnPoint=free->position+area.definition.origin;
    }
    const auto failed=[](DomainResult<> result)->std::optional<StepStatus> {
        if(result) return {};
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    };
    const auto guard=[&](bool active){return failed(setPd2Guard(controller,tick,active));};
    const auto cast=[&](size_t slot,std::optional<Vec> at={}) {
        const auto skill=unit.rule.skillIds[slot];const auto action=unit.rule.skillActions.find(skill);
        if(!skill || action==unit.rule.skillActions.end()) return StepStatus::NotImplemented;
        return usePd2Skill(controller,{id,skill,at?UnitTarget{}:target.value_or(UnitTarget{}),tick.tick,action->second.nativeMode,at});
    };
    if(controller.thirdParameter>3) {
        if(const auto error=guard(false)) return *error;
        controller.loop=controller.thirdParameter=0;
        return cast(1,returnPoint-area.definition.origin);
    }
    if(playerDistance>700 && controller.loop<1) {
        if(const auto error=guard(true)) return *error;
        controller.loop=1;
        const auto result=cast(4,returnPoint-area.definition.origin);
        ++controller.thirdParameter;return result;
    }
    if((bossDistance>650 && controller.loop<1) || (controller.loop==1 && bossDistance>400)) {
        const auto result=cast(4,returnPoint-area.definition.origin);
        ++controller.thirdParameter;return result;
    }
    const auto delay=std::max(1,int(unit.rule.ai.pd2Delays[size_t(unit.rule.difficulty)]));
    if(controller.loop==1) {
        const int radius=3+int(pd2CrtRandom(ports_.pd2Random)%3);
        auto goal=std::make_shared<std::optional<Vec>>();
        const auto moved=perform(controller,[this,id,position=unit.position,region=unit.area,generation=area.generation,radius,goal,tick,delay]()->DomainResult<> {
            if(!*goal) {
                const auto *actor=ports_.monsters.find(id);if(!actor) return {DomainStatus::InvalidActor,{}};
                auto random=actor->combatRandom;const auto point=monsterWanderPoint(position,radius,random);
                const auto consumed=ports_.monsters.consumeRandom(id,actor->combatRandom,random);if(!consumed) return consumed;
                *goal=point;
            }
            const auto move=ports_.monsters.requestMove({id,{region,generation,**goal},{},0,75,false,1});
            if(isDeferred(move.status)) return move;
            auto &control=state_.controllers.at(id);control.pursuing=bool(move);control.nextDecision=tick.tick+uint64_t(delay);
            return {DomainStatus::Applied,std::monostate{}};
        });
        if(const auto error=failed(moved)) return *error;
        if(playerDistance<=700) {
            if(const auto error=guard(false)) return *error;
            controller.loop=controller.thirdParameter=0;
        }
        return StepStatus::Complete;
    }
    if(const auto error=guard(false)) return *error;
    controller.loop=controller.thirdParameter=0;
    if(nearestDistance>600) {
        const auto roll=pd2CrtRandom(ports_.pd2Random)%100;
        if(roll<50) return cast(0);
        if(roll<80) return cast(4);
    }
    if(nearestDistance>400) {
        const auto roll=pd2CrtRandom(ports_.pd2Random)%100;
        if(roll<40) return cast(0);
        if(roll<80) return cast(1);
    }
    const auto roll=pd2CrtRandom(ports_.pd2Random)%100;
    if(roll<20) return cast(combat?3:1);
    if(roll<45) return cast(1);
    if(roll<50) return cast(0);
    if(roll<60) return cast(2);
    if(roll<80 && combat) {
        const auto result=requestAction(controller,{id,0,target.value_or(UnitTarget{}),tick.tick,4});
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto moved=perform(controller,[this,id,point,target,tick,delay,region=unit.area,generation=area.generation]()->DomainResult<> {
        const auto move=ports_.monsters.requestMove({id,{region,generation,point},target?target->id:EntityId{},0,75,false,5});
        if(isDeferred(move.status)) return move;
        auto &control=state_.controllers.at(id);control.pursuing=bool(move);control.nextDecision=tick.tick+uint64_t(delay);
        return {DomainStatus::Applied,std::monostate{}};
    });
    return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
