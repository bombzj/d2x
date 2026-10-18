#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::baalTentacleFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &params=unit.rule.ai.params;
    const auto *owner=ports_.monsters.find(unit.nativeOwner);const auto *player=ports_.players.findActor(unit.nativeOwner);
    const bool alive=owner?owner->alive():player && player->entered && player->persistent.player.hp>0;
    const auto kill=[&] {
        const auto result=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick);});
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(!alive) return kill();
    if(!controller.thirdParameter) {
        const auto seconds=nativeAdd(int32_t(limitedRandom(controller.random,uint32_t(params[2]))),params[2]);
        controller.thirdParameter=nativeAdd(nativeNarrow(uint32_t(tick.tick)),nativeMultiply(25,seconds));
    }
    if(uint32_t(tick.tick)>uint32_t(controller.thirdParameter)) return kill();
    const auto &area=ports_.areas.at(unit.area).definition;
    if(target && unitMeleeReach(area.collision,unit.position,unit.rule.size,point,size,unit.rule.meleeRange) &&
       int(limitedRandom(controller.random,100))<params[0]) {
        const auto result=requestAction(controller,{id,0,*target,tick.tick,4});
        controller.nextDecision=tick.tick+1;return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[1]);
    return StepStatus::Complete;
}
}
