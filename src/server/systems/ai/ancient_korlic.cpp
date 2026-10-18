#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::korlicFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    if(!ports_.quests.read().actFive.ancientsActive) {idle(25);return StepStatus::Complete;}
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto selected=acquireNativeBossTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},unit,[&](EntityId candidate) {
        const auto other=participants.find(candidate);return other.id() && (other.player?other.player->area:other.monster->area)==unit.area?1:0;
    });
    if(!selected.target) {idle(25);return StepStatus::Complete;}
    const auto target=selected.target->unit;const auto point=selected.target->position;const int size=selected.target->size;
    const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent) {return int(limitedRandom(controller.random,100))<percent;};
    const auto cast=[&](uint16_t skill,uint8_t mode) {
        const auto result=requestAction(controller,{id,skill,target,tick.tick,mode});
        controller.nextDecision=tick.tick+1;return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto skill=unit.rule.skillIds[0];
    if(skill>0 && meleeDistance(unit.position,unit.rule.size,point,size)<params[0] && chance(params[1]))
        return cast(skill,unit.rule.skillActions.at(skill).nativeMode);
    if(unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange)) {
        if(chance(params[2])) return cast(0,(rollRandom(controller.random)&1)?4:5);
        idle(25);return StepStatus::Complete;
    }
    const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target.id,0,75,false,1});
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick+10;return StepStatus::Complete;
}
}
