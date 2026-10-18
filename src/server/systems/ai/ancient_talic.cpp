#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::talicFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    if(!ports_.quests.read().actFive.ancientsActive || ports_.effects.unitStates(id,tick.tick).contains(unit.rule.uninterruptableState)) {idle(25);return StepStatus::Complete;}
    if(!target) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto selected=acquireNativeBossTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},unit,[&](EntityId candidate) {
            const auto other=participants.find(candidate);return other.id() && (other.player?other.player->area:other.monster->area)==unit.area?1:0;
        });
        if(!selected.target) {idle(25);return StepStatus::Complete;}
        target=selected.target->unit;point=selected.target->position;size=selected.target->size;
    }
    const auto cast=[&](uint16_t skill,uint8_t mode,UnitTarget victim,std::optional<Vec> at={}) {
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode,at});
        controller.nextDecision=tick.tick+1;return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto chance=[&](int percent) {return int(limitedRandom(controller.random,100))<percent;};
    const int distance=meleeDistance(unit.position,unit.rule.size,point,size);
    const auto skill=unit.rule.skillIds[0];
    if(skill>0 && distance<params[0] && chance(params[1])) {
        const int x=int(std::floor(unit.position.x)),y=int(std::floor(unit.position.y));
        const int tx=int(std::floor(point.x)),ty=int(std::floor(point.y));
        const int divisor=std::max(1,unitCoordinateDistance(x,y,tx,ty));
        const Vec destination{float(tx+(tx-x)*params[3]/divisor),float(ty+(ty-y)*params[3]/divisor)};
        return cast(skill,unit.rule.skillActions.at(skill).nativeMode,{},destination);
    }
    if(unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) {
        if(chance(params[2])) return cast(0,(rollRandom(controller.random)&1)?4:5,*target);
        idle(25);return StepStatus::Complete;
    }
    const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target->id,0,75,false,1});
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick+10;return StepStatus::Complete;
}
}
