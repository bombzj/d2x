#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <cmath>
#include <string_view>

namespace d2x::server::ai {
namespace {
bool deadCatapultInRoom(const AreaStore &areas,const monsters::State &monsters,RegionId source,Vec observer,int x,int y,int nativeClass) {
    auto room=areas.roomAtPosition(source,observer,x,y);
    if(!room) for(const auto &[region,area]:areas.all()) {
        if(area.definition.act!=areas.at(source).definition.act) continue;
        if(const auto *bounds=area.definition.activation.room(Vec{float(x),float(y)}-area.definition.origin)) {
            room=RoomView{region,(int(area.definition.origin.x)+bounds->x)/5,(int(area.definition.origin.y)+bounds->y)/5};break;
        }
    }
    if(!room) return false;
    for(const auto id:areas.roomUnitOrder(*room)) {
        const auto found=monsters.actors.find(id);
        if(found!=monsters.actors.end() && found->second.rule.nativeClass==nativeClass && !found->second.alive()) return true;
    }
    const auto &area=areas.at(room->area).definition;
    const auto *bounds=area.activation.room(Vec{float(room->x*5),float(room->y*5)}-area.origin);
    if(!bounds) return false;
    const auto inactive=monsters.inactive.find({room->area,size_t(bounds-area.activation.rooms().data())});
    if(inactive!=monsters.inactive.end()) for(const auto &unit:inactive->second)
        if(unit.identity.nativeClass==nativeClass && unit.dead) return true;
    return false;
}
}
StepStatus System::catapultSpotterFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area).definition;
    const auto &params=unit.rule.ai.params;
    if(!controller.thirdParameter && limitedRandom(controller.random,100)<3) {
        const auto *room=area.activation.room(unit.position);if(!room) return StepStatus::NotImplemented;
        const auto direction=catapultDirection(unit.rule.monsterChainOffset);
        if(!direction) return StepStatus::NotImplemented;
        const auto offset=*direction;
        int x=int(area.origin.x)+room->x,y=int(area.origin.y)+room->y;
        for(int index=0;index<3;++index) {
            x=nativeSubtract(x,int(offset.x)*room->width);y=nativeSubtract(y,int(offset.y)*room->height);
            // SUnitInactive checks both live room chains and compressed corpses.
            if(deadCatapultInRoom(ports_.areas,ports_.monsters.read(),unit.area,unit.position,x,y,unit.rule.nativeClass-19)) {
                const auto killed=perform(controller,[this,id,tick] {
                    auto result=ports_.monsters.scriptDeath(id,tick.tick);
                    if(result.status==DomainStatus::Stale || result.status==DomainStatus::Conflict) result.status=DomainStatus::Capacity;
                    return result;
                });
                return isDeferred(killed.status)?StepStatus::Blocked:StepStatus::Complete;
            }
        }
    }
    if(!controller.thirdParameter) controller.thirdParameter=1;
    const int delay=int16_t(params[1]);
    const auto target=acquireTarget(unit,tick,false,false);
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+uint64_t(std::max(frames,1));};
    if(nativeAdd(controller.thirdParameter,delay)>nativeNarrow(uint32_t(tick.tick)) || !target ||
       int(limitedRandom(controller.random,100))>=params[0]) {idle(delay);return StepStatus::Complete;}
    if(controller.loop>0) --controller.loop;
    else {controller.phase=int(rollRandom(controller.random)%5);controller.loop=params[4];}
    const int radius=int16_t(params[3]);
    const int x=nativeAdd(int(std::floor(target->position.x+area.origin.x)),nativeSubtract(int32_t(limitedRandom(controller.random,uint32_t(2*radius))),radius));
    const int y=nativeAdd(int(std::floor(target->position.y+area.origin.y)),nativeSubtract(int32_t(limitedRandom(controller.random,uint32_t(2*radius))),radius));
    const auto *bounds=area.activation.room(unit.position);if(!bounds) return StepStatus::NotImplemented;
    const RoomView room{unit.area,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    const auto point=ports_.areas.nativeFreeCoordinates(room,x,y,2,0x805,ports_.spatial.nativeCollisionSample(),{},0,false,50,3);
    if(!point) {idle(15);return StepStatus::Complete;}
    const Vec destination=point->position+ports_.areas.at(point->room.area).definition.origin-area.origin+Vec{.5f,.5f};
    constexpr std::array<std::string_view,5> names{"catapult charged ball","catapult spike ball","catapultblizzard","catapultplague","catapultmeteor"};
    const auto skill=unit.rule.formulas->skillIds.find(std::string(names[size_t(controller.phase)]));
    if(skill==unit.rule.formulas->skillIds.end()) return StepStatus::NotImplemented;
    const auto result=perform(controller,[this,id,destination,skill=uint16_t(skill->second),tick] {
        const auto cast=ports_.skills.requestCast({id,skill,UnitTarget{},tick.tick,4,destination});
        if(!isDeferred(cast.status)) {
            auto &ai=state_.controllers.at(id);ai.thirdParameter=nativeNarrow(uint32_t(tick.tick));ai.nextDecision=tick.tick+1;
        }
        return cast;
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
