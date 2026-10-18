#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::minionFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    bool commanded=false;
    if(!controller.commands.empty()) {
        const auto &command=controller.commands.front();
        const auto victim=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(command.target.id);
        const bool alive=victim.player?victim.player->entered && victim.player->persistent.player.hp>0:victim.monster && victim.monster->alive();
        if(command.code==1 && nativeNarrow(uint32_t(command.until))>nativeNarrow(uint32_t(tick.tick)) && alive) {
            commanded=true;
            if(!target || target->id!=command.target.id) {
                target=command.target;
                if(const auto at=ports_.monsters.targetPosition(target->id,unit.area)) {point=at->first;size=at->second;}
            }
        } else controller.commands.pop_front(); // Free only the current command.
    }
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact) {
        if(commanded || chance(params[0])) {
            // Fn116 reads aip2 here as well as for the melee idle duration.
            // The apparent aip5 attack-mode chance is not used by retail.
            const auto mode=chance(params[1])?uint8_t(5):uint8_t(4);
            const auto cast=requestAction(controller,{id,0,*target,tick.tick,mode});
            controller.nextDecision=tick.tick+(cast?1:10);return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[1]);
    } else if(commanded || chance(params[2])) {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},target?target->id:EntityId{},0,75,false,1});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else {ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(params[3]);}
    return StepStatus::Complete;
}
}
