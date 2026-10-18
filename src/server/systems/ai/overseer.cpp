#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
void System::commandMinions(EntityId source,UnitTarget target,uint64_t until) {
    const auto *owner=ports_.monsters.find(ports_.monsters.minionOwner(source));if(!owner) return;
    for(const auto id:owner->nativeMinions) if(const auto *minion=ports_.monsters.find(id)) {
        auto [entry,fresh]=state_.controllers.try_emplace(id);auto &controller=entry->second;
        if(fresh || controller.admissionRevision!=minion->admissionRevision) {
            controller={};controller.actor=id;controller.random=minion->combatRandom;controller.admissionRevision=minion->admissionRevision;
            controller.observedAiFunctionRevision=minion->aiFunctionRevision;
        }
        if(controller.observedAiFunctionRevision!=minion->aiFunctionRevision) {
            // The native switch precedes CopyAiCommand. Apply its parameter
            // reset before inserting this command, rather than deleting the
            // newly delivered command at the recipient's next think.
            controller.observedAiFunctionRevision=minion->aiFunctionRevision;
            controller.commands.clear();controller.phase=controller.loop=controller.thirdParameter=0;
            controller.curseParameters.fill(0);controller.commanded=controller.charged=controller.pursuing=false;
            controller.target.reset();controller.carrier.reset();controller.nextPath=0;controller.regenerationBonus=0;
            const auto ai=minion->curseControl.ai;
            controller.function=ai==CurseAi::DimVision || ai==CurseAi::Terror || ai==CurseAi::Taunt?ai:CurseAi::None;
        }
        controller.commands.push_front({1,target,until});
    }
}
StepStatus System::overseerFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto activated=ports_.quests.activateSiegeBoss(id,tick.tick);if(isDeferred(activated.status)) return StepStatus::Blocked;
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto has=[&](size_t slot){return unit.rule.skillIds[slot] && unit.rule.skillActions.contains(unit.rule.skillIds[slot]);};
    const auto cast=[&](int slot,UnitTarget victim,uint8_t mode=4) {
        const auto skill=slot<0?uint16_t{}:unit.rule.skillIds[size_t(slot)];
        if(slot>=0) mode=unit.rule.skillActions.at(skill).nativeMode;
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto attacker=participants.find(unit.lastAttacker);
    const bool livingAttacker=attacker.player?attacker.player->entered && attacker.player->persistent.player.hp>0:
        attacker.monster && attacker.monster->alive() && participants.canHarm(id,attacker.id());
    if(has(0) && (unit.nativeAiState==3 || unit.nativeAiState==19) && nativeNarrow(uint32_t(tick.tick))>controller.phase && livingAttacker) {
        controller.phase=nativeAdd(nativeNarrow(uint32_t(tick.tick)),params[0]);
        return cast(0,{attacker.id(),0,uint8_t(attacker.player?0:1)});
    }
    const auto primary=target.value_or(UnitTarget{});
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(contact && chance(params[5])) return cast(-1,primary,chance(params[6])?5:4);
    std::optional<UnitTarget> healing,whipping;int count{};
    if(const auto *room=area.definition.activation.room(unit.position))
        for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                if(candidate==id || other->rule.ai.kind!=MonsterAiKind::Minion || !other->alive() || participants.canHarm(id,candidate) ||
                   ports_.effects.unitStates(candidate,tick.tick).contains(unit.rule.bloodlustState)) continue;
                ++count;
                const auto at=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                const int x=int(std::floor(unit.position.x))-int(std::floor(at.x)),y=int(std::floor(unit.position.y))-int(std::floor(at.y));
                const auto square=nativeAdd(nativeMultiply(x,x),nativeMultiply(y,y));
                if(!healing && square<=576 && playerLifePercentage(other->life,other->maximumLife)<50) healing=UnitTarget{candidate,0,1};
                if(!whipping && square<=400 && !nativeUniqueMonster(other->identity.rank)) whipping=UnitTarget{candidate,0,1};
            }
    if(has(1) && healing && chance(params[1])) return cast(1,*healing);
    if(has(2) && whipping && chance(params[2]) && unit.alignment()!=UnitAlignment::Good) return cast(2,*whipping);
    if(count) {
        if(distance<params[3]-params[4]) {
            const int retreat=uint8_t(params[3]-distance);
            const auto destination=monsterRetreatPoint(unit.position,point,retreat);
            const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,destination},{},0,75,false,0,0,0,2});
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
        }
        if(distance>params[3]+params[4]) {
            const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},primary.id,0,75,false,1});
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
        }
        if(chance(50)) {
            const int turn=(rollRandom(controller.random)&255)<128?1:-1;
            const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},primary.id,0,75,false,0,turn,5});
            controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
        }
    } else if(chance(60)) {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,point},primary.id,0,75,false,1});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);return StepStatus::Complete;
    }
    ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+10;return StepStatus::Complete;
}
}
